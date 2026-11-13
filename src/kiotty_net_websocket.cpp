#include "kiotty_net_websocket.h"

#include "context/kiotty_context.h"
#include "context/kiotty_handles.h"
#include "context/kiotty_http_configuration.h"
#include "context/kiotty_websocket_endpoint.h"
#include "core/kiotty_text.h"
#include "domain/usecases/http/kiotty_http_token.h"
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

namespace
{
    KiottyNetWebSocketRoute failedRoute(KiottyNetWebSocketRoute route, KiottyNetCode code)
    {
        route.code = code;
        return route;
    }

    net::SocketTargetResult targetOf(KiottyNetWebSocketRoute route, bool has_action)
    {
        using TargetResult = net::SocketTargetResult;
        if (route.code != KIOTTY_NET_CODE_SUCCESS) { return TargetResult::failure(route.code); }
        if (route.server == nullptr || !has_action) { return TargetResult::failure(INVALID); }
        const net::RegistryResult registry = route.server->registry();
        if (!registry) { return TargetResult::failure(registry.code); }
        return registry.value().find(route.index);
    }
}

KiottyNetWebSocketRoute KiottyNet_addWebSocket(KiottyNetHttpServerHandle server, const char* path)
{
    const KiottyNetWebSocketRoute route = { INVALID, server, net::NO_SOCKET_PATH };
    if (server == nullptr) { return route; }
    const net::RegistryResult registry = server->registry();
    if (!registry) { return failedRoute(route, registry.code); }
    const net::SocketIndexResult added = registry.value().addRoute(path);
    if (!added) { return failedRoute(route, added.code); }
    const KiottyNetWebSocketRoute made = { KIOTTY_NET_CODE_SUCCESS, server, added.value };
    return made;
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketUpgrade(KiottyNetWebSocketRoute route,
                                                     KiottyNetWebSocketUpgradeAction on_upgrade)
{
    const net::SocketTargetResult target = targetOf(route, on_upgrade != nullptr);
    if (!target) { return failedRoute(route, target.code); }
    target.value().on_upgrade = on_upgrade;
    return route;
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketOpen(KiottyNetWebSocketRoute route,
                                                  KiottyNetWebSocketOpenAction on_open)
{
    const net::SocketTargetResult target = targetOf(route, on_open != nullptr);
    if (!target) { return failedRoute(route, target.code); }
    target.value().on_open = on_open;
    return route;
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketMessage(KiottyNetWebSocketRoute route,
                                                     KiottyNetWebSocketMessageAction on_message)
{
    const net::SocketTargetResult target = targetOf(route, on_message != nullptr);
    if (!target) { return failedRoute(route, target.code); }
    target.value().on_message = on_message;
    return route;
}

KiottyNetWebSocketRoute KiottyNet_onWebSocketClose(KiottyNetWebSocketRoute route,
                                                   KiottyNetWebSocketCloseAction on_close)
{
    const net::SocketTargetResult target = targetOf(route, on_close != nullptr);
    if (!target) { return failedRoute(route, target.code); }
    target.value().on_close = on_close;
    return route;
}

KiottyNetCode KiottyNet_broadcastWebSocket(KiottyNetHttpServerHandle server,
                                           const char* path,
                                           KiottyNetWebSocketOpcode opcode,
                                           KiottyNetSpan data)
{
    if (server == nullptr) { return INVALID; }
    return net::WebSocketAccess(server->site()).broadcast(path, opcode, data);
}

namespace
{
    constexpr size_t MAX_PATH_TEXT = KIOTTY_NET_MAX_URL_LENGTH - 1;

    bool isRequestTarget(net::Span<const char> path)
    {
        if (path.empty() || path[0] != '/') { return false; }
        for (const char character : path)
        {
            if (!net::isVisibleCharacter(character)) { return false; }
        }
        return true;
    }

    bool isProtocolList(net::Span<const char> list)
    {
        size_t start = 0;
        while (start <= list.length)
        {
            const size_t comma = net::findCharacter(list, ',', start);
            const net::Span<const char> item{ list.data + start, comma - start };
            if (!net::isToken(net::trimWhitespace(item))) { return false; }
            start = comma + 1;
        }
        return true;
    }

    KiottyNetCode limitsOf(const KiottyNetHttpLimits* limits, KiottyNetHttpLimits& chosen)
    {
        if (limits == nullptr) { return net::initializeHttpLimits(chosen); }
        chosen = *limits;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    net::RegistrationResult registrationOf(KiottyNetWebSocketClientHandle client, bool has_action)
    {
        using Registration = net::RegistrationResult;
        if (client == nullptr || !has_action) { return Registration::failure(INVALID); }
        return client->registration();
    }
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
    if (protocols == nullptr) { return client->setProtocols(net::Span<const char>{ nullptr, 0 }); }
    const net::TextResult text = net::parseText(protocols, MAX_PROTOCOL_TEXT);
    if (!text) { return INVALID; }
    const bool is_list = text.value.empty() || isProtocolList(text.value);
    if (!is_list) { return INVALID; }
    return client->setProtocols(text.value);
}

KiottyNetCode KiottyNet_onWebSocketClientOpen(KiottyNetWebSocketClientHandle client,
                                              KiottyNetWebSocketClientOpenAction on_open)
{
    const net::RegistrationResult site = registrationOf(client, on_open != nullptr);
    if (!site) { return site.code; }
    site.value().upgrade.events.on_open = on_open;
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode KiottyNet_onWebSocketClientMessage(KiottyNetWebSocketClientHandle client,
                                                 KiottyNetWebSocketClientMessageAction on_message)
{
    const net::RegistrationResult site = registrationOf(client, on_message != nullptr);
    if (!site) { return site.code; }
    site.value().upgrade.events.on_message = on_message;
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode KiottyNet_onWebSocketClientClose(KiottyNetWebSocketClientHandle client,
                                               KiottyNetWebSocketClientCloseAction on_close)
{
    const net::RegistrationResult site = registrationOf(client, on_close != nullptr);
    if (!site) { return site.code; }
    site.value().upgrade.events.on_close = on_close;
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode KiottyNet_onWebSocketClientError(KiottyNetWebSocketClientHandle client,
                                               KiottyNetWebSocketClientErrorAction on_error)
{
    const net::RegistrationResult site = registrationOf(client, on_error != nullptr);
    if (!site) { return site.code; }
    site.value().notices.setError(on_error);
    return KIOTTY_NET_CODE_SUCCESS;
}

KiottyNetCode KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle client,
                                           const KiottyNetAddress* address,
                                           const char* path,
                                           const KiottyNetTlsClientConfiguration* tls,
                                           KiottyNetAppContext app_context)
{
    if (client == nullptr || address == nullptr) { return INVALID; }
    const net::TextResult text = net::parseText(path, MAX_PATH_TEXT);
    if (!text || !isRequestTarget(text.value)) { return INVALID; }
    const KiottyNetAddress copied_address = *address;
    const net::ClientRun run{ copied_address, text.value, tls, app_context };
    return client->run(run);
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
    return KiottyNet_closeWebSocket(client->socket(), status, reason);
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
    return KiottyNet_sendWebSocket(client->socket(), opcode, data);
}
