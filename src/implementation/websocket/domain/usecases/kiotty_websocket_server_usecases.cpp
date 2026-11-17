#include "websocket/domain/usecases/kiotty_websocket_server_usecases.h"

#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "websocket/data/repository/endpoint/kiotty_websocket_endpoint.h"
#include "websocket/data/repository/server/kiotty_websocket_access.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_CLOSE_REASON_TEXT = KIOTTY_NET_MAX_WEB_SOCKET_REASON_LENGTH - 1;

        TextResult closeReasonOf(const char* reason)
        {
            if (reason == nullptr) { return TextResult::success(Span<const char>{ nullptr, 0 }); }
            return parseText(reason, MAX_CLOSE_REASON_TEXT);
        }

        SocketTargetResult routeTargetOf(WebSocketServer& server, int32_t index, bool has_action)
        {
            if (!has_action) { return SocketTargetResult::failure(INVALID); }
            const RegistryResult registry = server.registry();
            if (!registry) { return SocketTargetResult::failure(registry.code); }
            return server.target(index);
        }
    }

    KiottyNetCode sendWebSocket(KiottyNetWebSocketHandle websocket,
                                KiottyNetWebSocketOpcode opcode,
                                KiottyNetSpan data)
    {
        if (websocket.owner == nullptr) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        return websocket.owner->sendSocket(websocket.id, opcode, data);
    }

    KiottyNetCode closeWebSocket(KiottyNetWebSocketHandle websocket,
                                 uint16_t status,
                                 const char* reason)
    {
        if (websocket.owner == nullptr) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        const TextResult text = closeReasonOf(reason);
        if (!text) { return INVALID; }
        return websocket.owner->closeSocket(websocket.id, status, text.value);
    }

    UpgradeIndexResult addWebSocket(WebSocketServer& server, const char* path)
    {
        const RegistryResult registry = server.registry();
        if (!registry) { return UpgradeIndexResult::failure(registry.code); }
        const UpgradeIndexResult added = registry.value().addRoute(path);
        if (added) { server.addTarget(added.value); }
        return added;
    }

    KiottyNetCode onWebSocketUpgrade(WebSocketServer& server,
                                     int32_t index,
                                     KiottyNetWebSocketUpgradeAction on_upgrade)
    {
        const SocketTargetResult target = routeTargetOf(server, index, on_upgrade != nullptr);
        if (!target) { return target.code; }
        target.value().on_upgrade = on_upgrade;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onWebSocketOpen(WebSocketServer& server,
                                  int32_t index,
                                  KiottyNetWebSocketOpenAction on_open)
    {
        const SocketTargetResult target = routeTargetOf(server, index, on_open != nullptr);
        if (!target) { return target.code; }
        target.value().on_open = on_open;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onWebSocketMessage(WebSocketServer& server,
                                     int32_t index,
                                     KiottyNetWebSocketMessageAction on_message)
    {
        const SocketTargetResult target = routeTargetOf(server, index, on_message != nullptr);
        if (!target) { return target.code; }
        target.value().on_message = on_message;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode onWebSocketClose(WebSocketServer& server,
                                   int32_t index,
                                   KiottyNetWebSocketCloseAction on_close)
    {
        const SocketTargetResult target = routeTargetOf(server, index, on_close != nullptr);
        if (!target) { return target.code; }
        target.value().on_close = on_close;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode broadcastWebSocket(WebSocketServer& server,
                                     const char* path,
                                     KiottyNetWebSocketOpcode opcode,
                                     KiottyNetSpan data)
    {
        HttpSessionHost& host = server.host();
        WebSocketAccess access(host.registry, server, host.push_capacity);
        return access.broadcast(path, opcode, data);
    }
}
