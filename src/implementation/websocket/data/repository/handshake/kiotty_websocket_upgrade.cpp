#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "websocket/data/repository/handshake/kiotty_websocket_key_rules.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
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

        template <size_t N>
        Span<const char> headerNamed(Span<const KiottyNetHttpHeader> headers,
                                     const char (&name)[N])
        {
            return headerOf(headers, literalOf(name));
        }

        bool                hasToken(Span<const char> list,
                                     Span<const char> token,
                                     TokenMatch match);
        bool                isSameToken(Span<const char> left,
                                        Span<const char> right,
                                        TokenMatch match);
        UpgradeCheck        checkUpgrade(const KiottyNetHttpRequest& request);
        bool                callUpgrade(WebSocketHandler& handler, const UpgradeSession& upgrade);
        KiottyNetSpan       publicSpanOf(Span<const char> text);
        UpgradeAnswer       refuse(const UpgradeSession& upgrade, int32_t status);
        UpgradeAnswer       refuseVersion(const UpgradeSession& upgrade, int32_t status);
        UpgradeAnswer       acceptUpgrade(WebSocketHandler& handler, const UpgradeSession& upgrade);
    }

    UpgradeAnswer answerUpgrade(WebSocketHandler& handler,
                                const UpgradeSession& upgrade,
                                const WebSocketTarget& target)
    {
        const KiottyNetHttpRequest& request = upgrade.request;
        const bool is_get = request.method == KIOTTY_NET_HTTP_METHOD_GET;
        if (!is_get) { return refuse(upgrade, KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED); }
        if (upgrade.has_body) { return refuse(upgrade, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST); }
        const UpgradeCheck check = checkUpgrade(request);
        switch (check)
        {
        case UpgradeCheck::VALID:
            break;
        case UpgradeCheck::REQUIRED:
            return refuseVersion(upgrade, KIOTTY_NET_HTTP_STATUS_UPGRADE_REQUIRED);
        case UpgradeCheck::BAD:
            return refuse(upgrade, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST);
        }
        SocketState& socket = socketOf(handler, upgrade);
        socket.reset();
        socket.target = target;
        socket.upgrading = &upgrade;
        const bool is_allowed = callUpgrade(handler, upgrade);
        socket.upgrading = nullptr;
        if (!is_allowed) { return refuse(upgrade, KIOTTY_NET_HTTP_STATUS_FORBIDDEN); }
        return acceptUpgrade(handler, upgrade);
    }

    KiottyNetWebSocketHandle socketHandleOf(const WebSocketHandler& handler,
                                            const UpgradeSession& upgrade)
    {
        return KiottyNetWebSocketHandle{ handler.owner, upgrade.id.value() };
    }

    Span<const char> headerOf(const KiottyNetHttpRequest& request, Span<const char> name)
    {
        const Span<const KiottyNetHttpHeader> headers{ request.headers, request.header_count };
        return headerOf(headers, name);
    }

    Span<const char> headerOf(Span<const KiottyNetHttpHeader> headers, Span<const char> name)
    {
        for (const KiottyNetHttpHeader& header : headers)
        {
            const Span<const char> found{ header.name.data, header.name.length };
            if (equalsIgnoreCase(found, name)) { return spanOf(header.value); }
        }
        return Span<const char>{ nullptr, 0 };
    }

    KiottyNetCode chooseProtocol(WebSocketHandler& handler,
                                 const UpgradeSession& upgrade,
                                 Span<const char> protocol)
    {
        const KiottyNetHttpRequest& request = upgrade.request;
        const Span<const char> offered = headerNamed(request, "Sec-WebSocket-Protocol");
        const bool is_offered = hasToken(offered, protocol, TokenMatch::EXACT);
        if (!is_offered) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        (void)socketOf(handler, upgrade).protocol.assign(protocol);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isUpgradeResponse(Span<const KiottyNetHttpHeader> response,
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

        bool callUpgrade(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            const WebSocketTarget& target = socketOf(handler, upgrade).target;
            if (target.on_upgrade == nullptr) { return true; }
            const KiottyNetHttpRequest& request = upgrade.request;
            const KiottyNetWebSocketUpgrade offer{
                request.path,
                request.query,
                publicSpanOf(headerNamed(request, "Host")),
                publicSpanOf(headerNamed(request, "Origin")),
                publicSpanOf(headerNamed(request, "Sec-WebSocket-Protocol")),
            };
            const KiottyNetWebSocketHandle socket = socketHandleOf(handler, upgrade);
            const KiottyNetBool allowed = target.on_upgrade(upgrade.app_context, socket, offer);
            return allowed != 0;
        }

        UpgradeAnswer refuse(const UpgradeSession& upgrade, int32_t status)
        {
            upgrade.response.begin();
            (void)upgrade.response.setStatus(status);
            const Span<const char> get = literalOf("GET");
            const bool is_not_allowed = status == KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED;
            if (is_not_allowed) { (void)upgrade.response.addHeader(literalOf("Allow"), get); }
            return UpgradeAnswer::REFUSE;
        }

        UpgradeAnswer refuseVersion(const UpgradeSession& upgrade, int32_t status)
        {
            upgrade.response.begin();
            (void)upgrade.response.setStatus(status);
            (void)upgrade.response.addHeader(literalOf("Upgrade"), literalOf("websocket"));
            const Span<const char> version = literalOf(VERSION);
            (void)upgrade.response.addHeader(literalOf("Sec-WebSocket-Version"), version);
            return UpgradeAnswer::REFUSE;
        }

        UpgradeAnswer acceptUpgrade(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            const KiottyNetHttpRequest& request = upgrade.request;
            const AcceptText accept = acceptOf(headerNamed(request, "Sec-WebSocket-Key"));
            upgrade.response.begin();
            (void)upgrade.response.setStatus(KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS);
            (void)upgrade.response.addHeader(literalOf("Upgrade"), literalOf("websocket"));
            (void)upgrade.response.addHeader(literalOf("Sec-WebSocket-Accept"), accept.span());
            const Span<const char> protocol = socketOf(handler, upgrade).protocol.span();
            const Span<const char> protocol_name = literalOf("Sec-WebSocket-Protocol");
            if (!protocol.empty()) { (void)upgrade.response.addHeader(protocol_name, protocol); }
            return UpgradeAnswer::ACCEPT;
        }
    }
}
