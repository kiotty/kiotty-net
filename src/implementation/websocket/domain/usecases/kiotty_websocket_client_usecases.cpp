#include "websocket/domain/usecases/kiotty_websocket_client_usecases.h"

#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"
#include "websocket/domain/usecases/kiotty_websocket_server_usecases.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_CLIENT_PROTOCOL_TEXT = KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH - 1;

        bool isUpgradeTarget(Span<const char> path)
        {
            if (path.empty() || path[0] != '/') { return false; }
            for (const char character : path)
            {
                if (!isVisibleCharacter(character)) { return false; }
            }
            return true;
        }

        bool isProtocolNameList(Span<const char> list)
        {
            size_t start = 0;
            while (start <= list.length)
            {
                const size_t comma = findCharacter(list, ',', start);
                const Span<const char> item{ list.data + start, comma - start };
                if (!isToken(trimWhitespace(item))) { return false; }
                start = comma + 1;
            }
            return true;
        }

        RegistrationResult clientHostOf(WebSocketClient& client, bool has_action)
        {
            if (!has_action) { return RegistrationResult::failure(INVALID); }
            return client.registration();
        }
    }

    KiottyNetCode setWebSocketClientProtocols(WebSocketClient& client, const char* protocols)
    {
        if (protocols == nullptr) { return client.setProtocols(Span<const char>{ nullptr, 0 }); }
        const TextResult text = parseText(protocols, MAX_CLIENT_PROTOCOL_TEXT);
        if (!text) { return INVALID; }
        const bool is_list = text.value.empty() || isProtocolNameList(text.value);
        if (!is_list) { return INVALID; }
        return client.setProtocols(text.value);
    }

    KiottyNetCode onWebSocketClientOpen(WebSocketClient& client,
                                        KiottyNetWebSocketClientOpenAction on_open)
    {
        const RegistrationResult host = clientHostOf(client, on_open != nullptr);
        if (!host) { return host.code; }
        host.value().handler->client.events.on_open = on_open;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onWebSocketClientMessage(WebSocketClient& client,
                                           KiottyNetWebSocketClientMessageAction on_message)
    {
        const RegistrationResult host = clientHostOf(client, on_message != nullptr);
        if (!host) { return host.code; }
        host.value().handler->client.events.on_message = on_message;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onWebSocketClientClose(WebSocketClient& client,
                                         KiottyNetWebSocketClientCloseAction on_close)
    {
        const RegistrationResult host = clientHostOf(client, on_close != nullptr);
        if (!host) { return host.code; }
        host.value().handler->client.events.on_close = on_close;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onWebSocketClientError(WebSocketClient& client,
                                         KiottyNetWebSocketClientErrorAction on_error)
    {
        const RegistrationResult host = clientHostOf(client, on_error != nullptr);
        if (!host) { return host.code; }
        host.value().on_error = on_error;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode runWebSocketClient(WebSocketClient& client, const ClientRun& run)
    {
        if (!isUpgradeTarget(run.path)) { return INVALID; }
        return client.run(run);
    }

    KiottyNetCode byeWebSocketClient(WebSocketClient& client, uint16_t status, const char* reason)
    {
        return closeWebSocket(client.socket(), status, reason);
    }

    KiottyNetCode sendWebSocketClient(WebSocketClient& client,
                                      KiottyNetWebSocketOpcode opcode,
                                      KiottyNetSpan data)
    {
        return sendWebSocket(client.socket(), opcode, data);
    }
}
