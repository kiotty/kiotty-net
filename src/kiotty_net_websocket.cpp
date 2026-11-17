#include "kiotty_net_websocket.h"

#include "context/kiotty_context.h"
#include "context/kiotty_handles.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/server/kiotty_http_configuration.h"
#include "websocket/data/repository/endpoint/kiotty_websocket_endpoint.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"
#include "websocket/domain/usecases/kiotty_websocket_client_usecases.h"
#include "websocket/domain/usecases/kiotty_websocket_server_usecases.h"

namespace net = kiotty::net::implementation;

namespace
{
    using net::INVALID;
    constexpr KiottyNetCode PEER_CLOSED = KIOTTY_NET_CODE_PEER_CLOSED;
    constexpr KiottyNetSpan NONE = { nullptr, 0 };
    constexpr size_t        MAX_PROTOCOL_TEXT = KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH - 1;
    constexpr size_t        MAX_URL_PATH_TEXT = KIOTTY_NET_MAX_URL_LENGTH - 1;

    KiottyNetWebSocketRoute failedRoute(KiottyNetWebSocketRoute route, KiottyNetCode code)
    {
        route.code = code;
        return route;
    }

    KiottyNetWebSocketRoute routeAfter(KiottyNetWebSocketRoute route, KiottyNetCode code)
    {
        if (code != KIOTTY_NET_CODE_SUCCESS) { return failedRoute(route, code); }
        return route;
    }

    KiottyNetCode limitsOf(const KiottyNetHttpLimits* limits, KiottyNetHttpLimits& chosen)
    {
        if (limits == nullptr) { return net::initializeHttpLimits(chosen); }
        chosen = *limits;
        return KIOTTY_NET_CODE_SUCCESS;
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
    const net::TextResult wanted = net::parseText(name, net::MAX_HEADER_NAME_TEXT);
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
    return net::sendWebSocket(websocket, opcode, data);
}

KiottyNetCode KiottyNet_closeWebSocket(KiottyNetWebSocketHandle websocket,
                                       uint16_t status,
                                       const char* reason)
{
    return net::closeWebSocket(websocket, status, reason);
}

KiottyNetWebSocketRoute KiottyNet_addWebSocket(KiottyNetHttpServerHandle server, const char* path)
{
    const KiottyNetWebSocketRoute route = { INVALID, server, net::NO_SOCKET_PATH };
    if (server == nullptr) { return route; }
    const net::UpgradeIndexResult added = net::addWebSocket(*server, path);
    if (!added) { return failedRoute(route, added.code); }
    const KiottyNetWebSocketRoute made = { KIOTTY_NET_CODE_SUCCESS, server, added.value };
    return made;
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketUpgrade(KiottyNetWebSocketRoute route,
                                                     KiottyNetWebSocketUpgradeAction on_upgrade)
{
    if (route.code != KIOTTY_NET_CODE_SUCCESS) { return route; }
    if (route.server == nullptr) { return failedRoute(route, INVALID); }
    return routeAfter(route, net::onWebSocketUpgrade(*route.server, route.index, on_upgrade));
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketOpen(KiottyNetWebSocketRoute route,
                                                  KiottyNetWebSocketOpenAction on_open)
{
    if (route.code != KIOTTY_NET_CODE_SUCCESS) { return route; }
    if (route.server == nullptr) { return failedRoute(route, INVALID); }
    return routeAfter(route, net::onWebSocketOpen(*route.server, route.index, on_open));
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketMessage(KiottyNetWebSocketRoute route,
                                                     KiottyNetWebSocketMessageAction on_message)
{
    if (route.code != KIOTTY_NET_CODE_SUCCESS) { return route; }
    if (route.server == nullptr) { return failedRoute(route, INVALID); }
    return routeAfter(route, net::onWebSocketMessage(*route.server, route.index, on_message));
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketClose(KiottyNetWebSocketRoute route,
                                                   KiottyNetWebSocketCloseAction on_close)
{
    if (route.code != KIOTTY_NET_CODE_SUCCESS) { return route; }
    if (route.server == nullptr) { return failedRoute(route, INVALID); }
    return routeAfter(route, net::onWebSocketClose(*route.server, route.index, on_close));
}

KiottyNetCode KiottyNet_broadcastWebSocket(KiottyNetHttpServerHandle server,
                                           const char* path,
                                           KiottyNetWebSocketOpcode opcode,
                                           KiottyNetSpan data)
{
    if (server == nullptr) { return INVALID; }
    return net::broadcastWebSocket(*server, path, opcode, data);
}

KiottyNetWebSocketClientResult KiottyNet_createWebSocketClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* configuration,
    const KiottyNetHttpLimits* limits)
{
    KiottyNetWebSocketClientResult result = { INVALID, nullptr };
    if (context == nullptr || configuration == nullptr) { return result; }
    net::WebSocketClientConfiguration made{ *configuration, KiottyNetHttpLimits{} };
    const KiottyNetCode prepared = limitsOf(limits, made.limits);
    if (prepared != KIOTTY_NET_CODE_SUCCESS) { return result; }
    const net::WebSocketClientResult created = context->create(made);
    result.code = created.code;
    if (created) { result.handle = &created.value(); }
    return result;
}

void KiottyNet_destroyWebSocketClient(KiottyNetContext context,
                                      KiottyNetWebSocketClientHandle client)
{
    if (context == nullptr || client == nullptr) { return; }
    context->destroy(*client);
}

KiottyNetCode KiottyNet_setWebSocketClientProtocols(KiottyNetWebSocketClientHandle client,
                                                    const char* protocols)
{
    if (client == nullptr) { return INVALID; }
    return net::setWebSocketClientProtocols(*client, protocols);
}

KiottyNetCode KiottyNet_onWebSocketClientOpen(KiottyNetWebSocketClientHandle client,
                                              KiottyNetWebSocketClientOpenAction on_open)
{
    if (client == nullptr) { return INVALID; }
    return net::onWebSocketClientOpen(*client, on_open);
}

KiottyNetCode KiottyNet_onWebSocketClientMessage(KiottyNetWebSocketClientHandle client,
                                                 KiottyNetWebSocketClientMessageAction on_message)
{
    if (client == nullptr) { return INVALID; }
    return net::onWebSocketClientMessage(*client, on_message);
}

KiottyNetCode KiottyNet_onWebSocketClientClose(KiottyNetWebSocketClientHandle client,
                                               KiottyNetWebSocketClientCloseAction on_close)
{
    if (client == nullptr) { return INVALID; }
    return net::onWebSocketClientClose(*client, on_close);
}

KiottyNetCode KiottyNet_onWebSocketClientError(KiottyNetWebSocketClientHandle client,
                                               KiottyNetWebSocketClientErrorAction on_error)
{
    if (client == nullptr) { return INVALID; }
    return net::onWebSocketClientError(*client, on_error);
}

KiottyNetCode KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle client,
                                           const KiottyNetAddress* address,
                                           const char* path,
                                           const KiottyNetTlsClientConfiguration* tls,
                                           KiottyNetAppContext app_context)
{
    if (client == nullptr || address == nullptr) { return INVALID; }
    const net::TextResult text = net::parseText(path, MAX_URL_PATH_TEXT);
    if (!text) { return INVALID; }
    const KiottyNetAddress copied_address = *address;
    const net::ClientRun run{ copied_address, text.value, tls, app_context };
    return net::runWebSocketClient(*client, run);
}

void KiottyNet_closeWebSocketClient(KiottyNetWebSocketClientHandle client)
{
    if (client == nullptr) { return; }
    client->close();
}

KiottyNetCode KiottyNet_byeWebSocketClient(KiottyNetWebSocketClientHandle client,
                                           uint16_t status,
                                           const char* reason)
{
    if (client == nullptr) { return INVALID; }
    return net::byeWebSocketClient(*client, status, reason);
}

KiottyNetCode KiottyNet_awaitWebSocketClient(KiottyNetWebSocketClientHandle client,
                                             uint64_t timeout_milliseconds)
{
    if (client == nullptr) { return INVALID; }
    return client->await(timeout_milliseconds);
}

KiottyNetWebSocketHandle KiottyNet_getWebSocketClientSocket(KiottyNetWebSocketClientHandle client)
{
    if (client == nullptr) { return KiottyNetWebSocketHandle{ nullptr, 0 }; }
    return client->socket();
}

KiottyNetCode KiottyNet_sendWebSocketClient(KiottyNetWebSocketClientHandle client,
                                            KiottyNetWebSocketOpcode opcode,
                                            KiottyNetSpan data)
{
    if (client == nullptr) { return INVALID; }
    return net::sendWebSocketClient(*client, opcode, data);
}
