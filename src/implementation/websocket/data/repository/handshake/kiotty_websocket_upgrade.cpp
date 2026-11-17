#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_message_framing_parser.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "websocket/data/repository/handshake/kiotty_websocket_key_rules.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;

        enum class UpgradeCheck : uint8_t
        {
            VALID,
            REQUIRED,
            BAD
        };

        enum class TokenMatch : uint8_t
        {
            EXACT,
            IGNORE_CASE
        };

        constexpr char VERSION[] = "13";

        template <size_t N>
        Span<const char> headerNamed(const KiottyNetHttpRequest& request,
                                     const char (&name)[N])
        {
            return headerOf(request, literalOf(name));
        }

        bool                hasToken(Span<const char> list,
                                     Span<const char> token,
                                     TokenMatch match);
        bool                isSameToken(Span<const char> left,
                                        Span<const char> right,
                                        TokenMatch match);
        UpgradeCheck        checkUpgrade(const KiottyNetHttpRequest& request);
        bool                callUpgrade(HttpSession& session);
        KiottyNetSpan       publicSpanOf(Span<const char> text);
        KiottyNetTransition refuse(HttpSession& session, int32_t status);
        KiottyNetTransition refuseVersion(HttpSession& session, int32_t status);
        KiottyNetTransition acceptUpgrade(HttpSession& session);
    }

    KiottyNetTransition answerUpgrade(HttpSession& session, const WebSocketTarget& target)
    {
        const KiottyNetHttpRequest& request = session.request.request();
        const bool is_get = request.method == KIOTTY_NET_HTTP_METHOD_GET;
        if (!is_get) { return refuse(session, KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED); }
        const bool has_body = hasBody(session.state.framing.body);
        if (has_body) { return refuse(session, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST); }
        const UpgradeCheck check = checkUpgrade(request);
        switch (check)
        {
        case UpgradeCheck::VALID:
            break;
        case UpgradeCheck::REQUIRED:
            return refuseVersion(session, KIOTTY_NET_HTTP_STATUS_UPGRADE_REQUIRED);
        case UpgradeCheck::BAD:
            return refuse(session, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST);
        }
        SocketState& socket = socketOf(session);
        socket.reset();
        socket.target = target;
        session.state.phase = SessionPhase::UPGRADING;
        const bool is_allowed = callUpgrade(session);
        if (!is_allowed) { return refuse(session, KIOTTY_NET_HTTP_STATUS_FORBIDDEN); }
        return acceptUpgrade(session);
    }

    KiottyNetWebSocketHandle socketHandleOf(const HttpSession& session)
    {
        return KiottyNetWebSocketHandle{ handlerOf(session).owner, session.id.value() };
    }

    Span<const char> headerOf(const KiottyNetHttpRequest& request, Span<const char> name)
    {
        for (size_t i = 0; i < request.header_count; ++i)
        {
            const KiottyNetHttpHeader& header = request.headers[i];
            const Span<const char> found{ header.name.data, header.name.length };
            if (equalsIgnoreCase(found, name)) { return spanOf(header.value); }
        }
        return Span<const char>{ nullptr, 0 };
    }

    KiottyNetCode chooseProtocol(HttpSession& session, Span<const char> protocol)
    {
        const KiottyNetHttpRequest& request = session.request.request();
        const Span<const char> offered = headerNamed(request, "Sec-WebSocket-Protocol");
        const bool is_offered = hasToken(offered, protocol, TokenMatch::EXACT);
        if (!is_offered) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        (void)socketOf(session).protocol.assign(protocol);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isUpgradeResponse(const KiottyNetHttpRequest& response,
                           Span<const char> accept,
                           Span<const char> offered)
    {
        const TokenMatch caseless = TokenMatch::IGNORE_CASE;
        const Span<const char> upgrade = headerNamed(response, "Upgrade");
        const Span<const char> connection = headerNamed(response, "Connection");
        const Span<const char> answered = headerNamed(response, "Sec-WebSocket-Accept");
        const Span<const char> protocol = headerNamed(response, "Sec-WebSocket-Protocol");
        const bool is_upgrade = hasToken(upgrade, literalOf("websocket"), caseless);
        const bool is_connection = hasToken(connection, literalOf("upgrade"), caseless);
        const bool is_accepted = isSameToken(answered, accept, TokenMatch::EXACT);
        const bool is_offered = protocol.empty() || hasToken(offered, protocol, TokenMatch::EXACT);
        return is_upgrade && is_connection && is_accepted && is_offered;
    }

    namespace
    {
        KiottyNetSpan publicSpanOf(Span<const char> text)
        {
            return KiottyNetSpan{ text.data, text.length };
        }

        bool hasToken(Span<const char> list, Span<const char> token, TokenMatch match)
        {
            size_t start = 0;
            while (start < list.length)
            {
                const size_t comma = findCharacter(list, ',', start);
                const Span<const char> item{ list.data + start, comma - start };
                if (isSameToken(trimWhitespace(item), token, match)) { return true; }
                start = comma + 1;
            }
            return false;
        }

        bool isSameToken(Span<const char> left, Span<const char> right, TokenMatch match)
        {
            if (left.empty() || left.length != right.length) { return false; }
            if (match == TokenMatch::IGNORE_CASE) { return equalsIgnoreCase(left, right); }
            return std::memcmp(left.data, right.data, left.length) == 0;
        }

        UpgradeCheck checkUpgrade(const KiottyNetHttpRequest& request)
        {
            const TokenMatch caseless = TokenMatch::IGNORE_CASE;
            const Span<const char> upgrade = headerNamed(request, "Upgrade");
            const Span<const char> connection = headerNamed(request, "Connection");
            const Span<const char> version = headerNamed(request, "Sec-WebSocket-Version");
            const Span<const char> key = headerNamed(request, "Sec-WebSocket-Key");
            const bool is_upgrade = hasToken(upgrade, literalOf("websocket"), caseless);
            const bool is_connection = hasToken(connection, literalOf("upgrade"), caseless);
            const bool is_version = isSameToken(version, literalOf(VERSION), TokenMatch::EXACT);
            const bool is_complete = is_upgrade && is_connection && is_version && !key.empty();
            if (!is_complete) { return UpgradeCheck::REQUIRED; }
            if (!isWebSocketKey(key)) { return UpgradeCheck::BAD; }
            return UpgradeCheck::VALID;
        }

        bool callUpgrade(HttpSession& session)
        {
            const WebSocketTarget& target = socketOf(session).target;
            if (target.on_upgrade == nullptr) { return true; }
            const KiottyNetHttpRequest& request = session.request.request();
            const KiottyNetWebSocketUpgrade upgrade{
                request.path,
                request.query,
                publicSpanOf(headerNamed(request, "Host")),
                publicSpanOf(headerNamed(request, "Origin")),
                publicSpanOf(headerNamed(request, "Sec-WebSocket-Protocol")),
            };
            const KiottyNetBool allowed =
                target.on_upgrade(session.host.app_context, socketHandleOf(session), upgrade);
            return allowed != 0;
        }

        KiottyNetTransition refuse(HttpSession& session, int32_t status)
        {
            session.response.begin();
            (void)session.response.setStatus(status);
            const Span<const char> get = literalOf("GET");
            const bool is_not_allowed = status == KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED;
            if (is_not_allowed) { (void)session.response.addHeader(literalOf("Allow"), get); }
            session.state.persistence = Persistence::CLOSE;
            return startSending(session);
        }

        KiottyNetTransition refuseVersion(HttpSession& session, int32_t status)
        {
            session.response.begin();
            (void)session.response.setStatus(status);
            (void)session.response.addHeader(literalOf("Upgrade"), literalOf("websocket"));
            const Span<const char> version = literalOf(VERSION);
            (void)session.response.addHeader(literalOf("Sec-WebSocket-Version"), version);
            session.state.persistence = Persistence::CLOSE;
            return startSending(session);
        }

        KiottyNetTransition acceptUpgrade(HttpSession& session)
        {
            const KiottyNetHttpRequest& request = session.request.request();
            const AcceptText accept = acceptOf(headerNamed(request, "Sec-WebSocket-Key"));
            session.response.begin();
            (void)session.response.setStatus(KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS);
            (void)session.response.addHeader(literalOf("Upgrade"), literalOf("websocket"));
            (void)session.response.addHeader(literalOf("Sec-WebSocket-Accept"), accept.span());
            const Span<const char> protocol = socketOf(session).protocol.span();
            const Span<const char> protocol_name = literalOf("Sec-WebSocket-Protocol");
            if (!protocol.empty()) { (void)session.response.addHeader(protocol_name, protocol); }
            session.state.persistence = Persistence::UPGRADE;
            advanceSession(session);
            const KiottyNetTransition sending = startSending(session);
            return Next::moveToNextState(HttpState::UPGRADED, sending.entry_action);
        }
    }
}
