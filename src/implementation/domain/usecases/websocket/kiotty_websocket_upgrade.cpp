#include "domain/usecases/websocket/kiotty_websocket_upgrade.h"

#include "core/kiotty_text.h"
#include "data/repository/http/kiotty_http_site.h"
#include "domain/entity/kiotty_action.h"
#include "domain/usecases/http/kiotty_http_exchange.h"
#include "domain/usecases/http/kiotty_http_token.h"
#include "domain/usecases/websocket/kiotty_websocket_handshake.h"

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
        Span<const char> literalOf(const char (&text)[N])
        {
            return Span<const char>{ text, N - 1 };
        }

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
        if (session.has_body) { return refuse(session, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST); }
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
        session.socket.reset();
        session.socket.target = target;
        session.phase = SessionPhase::UPGRADING;
        const bool is_allowed = callUpgrade(session);
        if (!is_allowed) { return refuse(session, KIOTTY_NET_HTTP_STATUS_FORBIDDEN); }
        return acceptUpgrade(session);
    }

    KiottyNetWebSocketHandle socketHandleOf(const HttpSession& session)
    {
        return KiottyNetWebSocketHandle{ session.site.socket_owner, session.id.value() };
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
        (void)session.socket.protocol.assign(protocol);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isUpgradeResponse(const KiottyNetHttpRequest& response, Span<const char> accept)
    {
        const TokenMatch caseless = TokenMatch::IGNORE_CASE;
        const Span<const char> upgrade = headerNamed(response, "Upgrade");
        const Span<const char> connection = headerNamed(response, "Connection");
        const Span<const char> answered = headerNamed(response, "Sec-WebSocket-Accept");
        const Span<const char> protocol = headerNamed(response, "Sec-WebSocket-Protocol");
        const bool is_upgrade = hasToken(upgrade, literalOf("websocket"), caseless);
        const bool is_connection = hasToken(connection, literalOf("upgrade"), caseless);
        const bool is_accepted = isSameToken(answered, accept, TokenMatch::EXACT);
        return is_upgrade && is_connection && is_accepted && protocol.empty();
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
            const WebSocketTarget& target = session.socket.target;
            if (target.handler.onUpgrade == nullptr) { return true; }
            const KiottyNetHttpRequest& request = session.request.request();
            const KiottyNetWebSocketUpgrade upgrade{
                request.path,
                request.query,
                publicSpanOf(headerNamed(request, "Host")),
                publicSpanOf(headerNamed(request, "Origin")),
                publicSpanOf(headerNamed(request, "Sec-WebSocket-Protocol")),
            };
            const KiottyNetBool allowed =
                target.handler.onUpgrade(target.app_context, socketHandleOf(session), upgrade);
            return allowed != 0;
        }

        KiottyNetTransition refuse(HttpSession& session, int32_t status)
        {
            session.response.begin();
            (void)session.response.setStatus(status);
            const Span<const char> get = literalOf("GET");
            const bool is_not_allowed = status == KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED;
            if (is_not_allowed) { (void)session.response.addHeader(literalOf("Allow"), get); }
            session.persistence = Persistence::CLOSE;
            return startSending(session);
        }

        KiottyNetTransition refuseVersion(HttpSession& session, int32_t status)
        {
            session.response.begin();
            (void)session.response.setStatus(status);
            (void)session.response.addHeader(literalOf("Upgrade"), literalOf("websocket"));
            const Span<const char> version = literalOf(VERSION);
            (void)session.response.addHeader(literalOf("Sec-WebSocket-Version"), version);
            session.persistence = Persistence::CLOSE;
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
            const Span<const char> protocol = session.socket.protocol.span();
            const Span<const char> protocol_name = literalOf("Sec-WebSocket-Protocol");
            if (!protocol.empty()) { (void)session.response.addHeader(protocol_name, protocol); }
            session.persistence = Persistence::UPGRADE;
            session.id.generation = nextSessionGeneration(session.id.generation);
            const KiottyNetTransition sending = startSending(session);
            return Next::moveToNextState(HttpState::SOCKET_OPENING, sending.entry_action);
        }
    }
}
