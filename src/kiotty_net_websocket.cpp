#include "kiotty_net_websocket.h"

#include "context/kiotty_handles.h"
#include "context/kiotty_websocket_endpoint.h"
#include "core/kiotty_text.h"
#include "domain/usecases/websocket/kiotty_websocket_access.h"

namespace net = kiotty::net::implementation;

namespace
{
    constexpr KiottyNetCode INVALID = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    constexpr KiottyNetCode PEER_CLOSED = KIOTTY_NET_CODE_PEER_CLOSED;
    constexpr KiottyNetSpan NONE = { nullptr, 0 };
    constexpr size_t        MAX_HEADER_NAME_TEXT = 256;
    constexpr size_t        MAX_PROTOCOL_TEXT = KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH - 1;
    constexpr size_t        MAX_REASON_TEXT = KIOTTY_NET_MAX_WEB_SOCKET_REASON_LENGTH - 1;

    net::TextResult reasonOf(const char* reason)
    {
        if (reason == nullptr) { return net::TextResult::success(net::Span<const char>{ nullptr, 0 }); }
        return net::parseText(reason, MAX_REASON_TEXT);
    }
}

KiottyNetWebSocketInfo KiottyNet_getWebSocketInfo(KiottyNetWebSocketHandle websocket)
{
    if (websocket.owner == nullptr) { return KiottyNetWebSocketInfo{}; }
    return websocket.owner->socketInfo(websocket.id);
}

KiottyNetSpan KiottyNet_findWebSocketUpgradeHeader(KiottyNetWebSocketHandle websocket,
                                                   const char* name)
{
    if (websocket.owner == nullptr) { return NONE; }
    const net::TextResult wanted = net::parseText(name, MAX_HEADER_NAME_TEXT);
    if (!wanted || wanted.value.empty()) { return NONE; }
    return websocket.owner->upgradeHeader(websocket.id, wanted.value);
}

KiottyNetCode KiottyNet_selectWebSocketProtocol(KiottyNetWebSocketHandle websocket,
                                                const char* protocol)
{
    if (websocket.owner == nullptr) { return PEER_CLOSED; }
    const net::TextResult wanted = net::parseText(protocol, MAX_PROTOCOL_TEXT);
    if (!wanted || wanted.value.empty()) { return INVALID; }
    return websocket.owner->selectProtocol(websocket.id, wanted.value);
}

KiottyNetCode KiottyNet_sendWebSocket(KiottyNetWebSocketHandle websocket,
                                      KiottyNetWebSocketOpcode opcode,
                                      KiottyNetSpan data)
{
    if (websocket.owner == nullptr) { return PEER_CLOSED; }
    return websocket.owner->sendSocket(websocket.id, opcode, data);
}

KiottyNetCode KiottyNet_closeWebSocket(KiottyNetWebSocketHandle websocket,
                                       uint16_t status,
                                       const char* reason)
{
    if (websocket.owner == nullptr) { return PEER_CLOSED; }
    const net::TextResult text = reasonOf(reason);
    if (!text) { return INVALID; }
    return websocket.owner->closeSocket(websocket.id, status, text.value);
}

KiottyNetCode KiottyNet_addWebSocket(KiottyNetHttpServerHandle server,
                                     const char* path,
                                     KiottyNetWebSocketHandler handler,
                                     KiottyNetAppContext app_context)
{
    if (server == nullptr) { return INVALID; }
    const net::RegistryResult registry = server->registry();
    if (!registry) { return registry.code; }
    return registry.value().addRoute(path, handler, app_context);
}

KiottyNetCode KiottyNet_broadcastWebSocket(KiottyNetHttpServerHandle server,
                                           const char* path,
                                           KiottyNetWebSocketOpcode opcode,
                                           KiottyNetSpan data)
{
    if (server == nullptr) { return INVALID; }
    return net::WebSocketAccess(server->site()).broadcast(path, opcode, data);
}
