#ifndef KIOTTY_NET_WEBSOCKET_H
#define KIOTTY_NET_WEBSOCKET_H

#include "kiotty_net_http.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIOTTY_NET_MAX_URL_PATH_LENGTH           256
#define KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH 128
#define KIOTTY_NET_MAX_WEB_SOCKET_REASON_LENGTH   124

typedef enum KiottyNetWebSocketOpcode
{
    KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT   = 1,
    KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY = 2
} KiottyNetWebSocketOpcode;

typedef enum KiottyNetWebSocketStatus
{
    KIOTTY_NET_WEB_SOCKET_STATUS_NORMAL_CLOSURE   = 1000,
    KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY       = 1001,
    KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR   = 1002,
    KIOTTY_NET_WEB_SOCKET_STATUS_UNSUPPORTED_DATA = 1003,
    KIOTTY_NET_WEB_SOCKET_STATUS_NO_STATUS        = 1005,
    KIOTTY_NET_WEB_SOCKET_STATUS_ABNORMAL_CLOSURE = 1006,
    KIOTTY_NET_WEB_SOCKET_STATUS_INVALID_PAYLOAD  = 1007,
    KIOTTY_NET_WEB_SOCKET_STATUS_POLICY_VIOLATION = 1008,
    KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG  = 1009,
    KIOTTY_NET_WEB_SOCKET_STATUS_INTERNAL_ERROR   = 1011
} KiottyNetWebSocketStatus;

typedef struct KiottyNetWebSocketMessage
{
    KiottyNetWebSocketOpcode opcode;
    KiottyNetSpan            data;
} KiottyNetWebSocketMessage;

typedef struct KiottyNetWebSocketClose
{
    KiottyNetCode code;
    uint16_t      status;
    KiottyNetSpan reason;
} KiottyNetWebSocketClose;

typedef struct KiottyNetWebSocketEndpoint KiottyNetWebSocketEndpoint;

typedef struct KiottyNetWebSocketHandle
{
    KiottyNetWebSocketEndpoint* owner;
    int64_t                     id;
} KiottyNetWebSocketHandle;

typedef void* KiottyNetWebSocketContext;

typedef struct KiottyNetWebSocketUpgrade
{
    KiottyNetSpan path;
    KiottyNetSpan query;
    KiottyNetSpan host;
    KiottyNetSpan origin;
    KiottyNetSpan protocols;
} KiottyNetWebSocketUpgrade;

typedef struct KiottyNetWebSocketInfo
{
    KiottyNetConnectionInfo connection;
    char                    protocol[KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH];
} KiottyNetWebSocketInfo;

typedef KiottyNetBool (*KiottyNetWebSocketUpgradeAction)(
    KiottyNetAppContext app_context,
    KiottyNetWebSocketHandle websocket,
    KiottyNetWebSocketUpgrade upgrade);
typedef KiottyNetWebSocketContext (*KiottyNetWebSocketOpenAction)(
    KiottyNetAppContext app_context,
    KiottyNetWebSocketHandle websocket);
typedef void (*KiottyNetWebSocketMessageAction)(
    KiottyNetWebSocketContext websocket_context,
    KiottyNetWebSocketHandle websocket,
    KiottyNetWebSocketMessage message);
typedef void (*KiottyNetWebSocketCloseAction)(
    KiottyNetAppContext app_context,
    KiottyNetWebSocketContext websocket_context,
    KiottyNetWebSocketClose close);

typedef struct KiottyNetWebSocketHandler
{
    KiottyNetWebSocketUpgradeAction onUpgrade;
    KiottyNetWebSocketOpenAction    onOpen;
    KiottyNetWebSocketMessageAction onMessage;
    KiottyNetWebSocketCloseAction   onClose;
} KiottyNetWebSocketHandler;

typedef struct KiottyNetWebSocketRoute
{
    KiottyNetCode             code;
    KiottyNetHttpServerHandle server;
    int32_t                   index;
} KiottyNetWebSocketRoute;

KIOTTY_NET_API KiottyNetWebSocketInfo KiottyNet_getWebSocketInfo(KiottyNetWebSocketHandle websocket);
KIOTTY_NET_API KiottyNetSpan          KiottyNet_findWebSocketUpgradeHeader(
    KiottyNetWebSocketHandle websocket,
    const char* name);
KIOTTY_NET_API KiottyNetCode          KiottyNet_selectWebSocketProtocol(
    KiottyNetWebSocketHandle websocket,
    const char* protocol);
KIOTTY_NET_API KiottyNetCode          KiottyNet_sendWebSocket(
    KiottyNetWebSocketHandle websocket,
    KiottyNetWebSocketOpcode opcode,
    KiottyNetSpan data);
KIOTTY_NET_API KiottyNetCode          KiottyNet_closeWebSocket(
    KiottyNetWebSocketHandle websocket,
    uint16_t status,
    const char* reason);

KIOTTY_NET_API KiottyNetWebSocketRoute KiottyNet_addWebSocket(
    KiottyNetHttpServerHandle server,
    const char* path);
KIOTTY_NET_API KiottyNetWebSocketRoute KiottyNet_onWebSocketUpgrade(
    KiottyNetWebSocketRoute route,
    KiottyNetWebSocketUpgradeAction on_upgrade);
KIOTTY_NET_API KiottyNetWebSocketRoute KiottyNet_onWebSocketOpen(
    KiottyNetWebSocketRoute route,
    KiottyNetWebSocketOpenAction on_open);
KIOTTY_NET_API KiottyNetWebSocketRoute KiottyNet_onWebSocketMessage(
    KiottyNetWebSocketRoute route,
    KiottyNetWebSocketMessageAction on_message);
KIOTTY_NET_API KiottyNetWebSocketRoute KiottyNet_onWebSocketClose(
    KiottyNetWebSocketRoute route,
    KiottyNetWebSocketCloseAction on_close);
KIOTTY_NET_API KiottyNetCode KiottyNet_broadcastWebSocket(
    KiottyNetHttpServerHandle server,
    const char* path,
    KiottyNetWebSocketOpcode opcode,
    KiottyNetSpan data);

typedef struct KiottyNetWebSocketClient* KiottyNetWebSocketClientHandle;

typedef struct KiottyNetWebSocketClientResult
{
    KiottyNetCode                  code;
    KiottyNetWebSocketClientHandle handle;
} KiottyNetWebSocketClientResult;

KIOTTY_NET_API KiottyNetWebSocketClientResult KiottyNet_createWebSocketClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* configuration,
    const KiottyNetHttpLimits* limits,
    const char* path);
KIOTTY_NET_API void                           KiottyNet_destroyWebSocketClient(
    KiottyNetContext context,
    KiottyNetWebSocketClientHandle client);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_runWebSocketClient(
    KiottyNetWebSocketClientHandle client,
    const KiottyNetAddress* address,
    const KiottyNetTlsClientConfiguration* tls,
    const KiottyNetWebSocketHandler* handler,
    KiottyNetAppContext app_context);
KIOTTY_NET_API void KiottyNet_closeWebSocketClient(
    KiottyNetWebSocketClientHandle client);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_awaitWebSocketClient(
    KiottyNetWebSocketClientHandle client,
    uint64_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetWebSocketHandle KiottyNet_getWebSocketClientSocket(
    KiottyNetWebSocketClientHandle client);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_sendWebSocketClient(
    KiottyNetWebSocketClientHandle client,
    KiottyNetWebSocketOpcode opcode,
    KiottyNetSpan data);

#ifdef __cplusplus
}

class KiottyNet_WebSocket
{
public:
    explicit KiottyNet_WebSocket(KiottyNetWebSocketHandle handle);

    KiottyNetWebSocketHandle handle() const;
    KiottyNetWebSocketInfo   info() const;
    KiottyNetSpan            upgradeHeader(const char* name) const;
    KiottyNetCode            selectProtocol(const char* protocol);
    KiottyNetCode            send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
    KiottyNetCode            close(
        uint16_t status = KIOTTY_NET_WEB_SOCKET_STATUS_NORMAL_CLOSURE,
        const char* reason = nullptr);

    bool operator==(const KiottyNet_WebSocket& other) const;
    bool operator!=(const KiottyNet_WebSocket& other) const;
    bool operator<(const KiottyNet_WebSocket& other) const;

private:
    KiottyNetWebSocketHandle _handle;
};

class KiottyNet_WebSocketRoute
{
public:
    KiottyNet_WebSocketRoute(KiottyNet_HttpServer& server, const char* path);
    KiottyNet_WebSocketRoute(KiottyNetHttpServerHandle server, const char* path);

    KiottyNet_WebSocketRoute& onUpgrade(KiottyNetWebSocketUpgradeAction on_upgrade);
    KiottyNet_WebSocketRoute& onOpen(KiottyNetWebSocketOpenAction on_open);
    KiottyNet_WebSocketRoute& onMessage(KiottyNetWebSocketMessageAction on_message);
    KiottyNet_WebSocketRoute& onClose(KiottyNetWebSocketCloseAction on_close);
    KiottyNetCode             build() const;
    KiottyNetWebSocketRoute   route() const;

private:
    KiottyNetWebSocketRoute _route;
};

class KiottyNet_WebSocketClient
{
public:
    KiottyNet_WebSocketClient(
        KiottyNetContext context,
        const KiottyNetClientConfiguration& configuration,
        const KiottyNetHttpLimits* limits,
        const char* path);

    explicit operator bool() const;
    KiottyNetCode                  code() const;
    KiottyNetWebSocketClientHandle handle() const;

    KiottyNetCode            run(
        const KiottyNetAddress& address,
        const KiottyNetWebSocketHandler& handler,
        KiottyNetAppContext app_context);
    KiottyNetCode            run(
        const KiottyNetAddress& address,
        const KiottyNetTlsClientConfiguration& tls,
        const KiottyNetWebSocketHandler& handler,
        KiottyNetAppContext app_context);
    void                     close();
    KiottyNetCode            await(uint64_t timeout_milliseconds);
    KiottyNetWebSocketHandle socket() const;
    KiottyNetCode            send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);

private:
    KiottyNet_WebSocketClient(KiottyNetContext context, KiottyNetWebSocketClientResult result);

    KiottyNetCode _code;
    kiotty::net::OwnedHandle<
        KiottyNetWebSocketClientHandle,
        KiottyNet_destroyWebSocketClient> _handle;
};

inline KiottyNet_WebSocket::KiottyNet_WebSocket(KiottyNetWebSocketHandle handle)
    : _handle(handle)
{
}

inline KiottyNetWebSocketHandle KiottyNet_WebSocket::handle() const
{
    return _handle;
}

inline KiottyNetWebSocketInfo KiottyNet_WebSocket::info() const
{
    return KiottyNet_getWebSocketInfo(_handle);
}

inline KiottyNetSpan KiottyNet_WebSocket::upgradeHeader(const char* name) const
{
    return KiottyNet_findWebSocketUpgradeHeader(_handle, name);
}

inline KiottyNetCode KiottyNet_WebSocket::selectProtocol(const char* protocol)
{
    return KiottyNet_selectWebSocketProtocol(_handle, protocol);
}

inline KiottyNetCode KiottyNet_WebSocket::send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data)
{
    return KiottyNet_sendWebSocket(_handle, opcode, data);
}

inline KiottyNetCode KiottyNet_WebSocket::close(uint16_t status, const char* reason)
{
    return KiottyNet_closeWebSocket(_handle, status, reason);
}

inline bool KiottyNet_WebSocket::operator==(const KiottyNet_WebSocket& other) const
{
    return _handle.owner == other._handle.owner && _handle.id == other._handle.id;
}

inline bool KiottyNet_WebSocket::operator!=(const KiottyNet_WebSocket& other) const
{
    return !(*this == other);
}

inline bool KiottyNet_WebSocket::operator<(const KiottyNet_WebSocket& other) const
{
    std::less<const KiottyNetWebSocketEndpoint*> before;
    if (before(_handle.owner, other._handle.owner))
    {
        return true;
    }
    if (before(other._handle.owner, _handle.owner))
    {
        return false;
    }
    return _handle.id < other._handle.id;
}

inline KiottyNet_WebSocketRoute::KiottyNet_WebSocketRoute(
    KiottyNet_HttpServer& server,
    const char* path)
    : _route(KiottyNet_addWebSocket(server.handle(), path))
{
}

inline KiottyNet_WebSocketRoute::KiottyNet_WebSocketRoute(
    KiottyNetHttpServerHandle server,
    const char* path)
    : _route(KiottyNet_addWebSocket(server, path))
{
}

inline KiottyNet_WebSocketRoute& KiottyNet_WebSocketRoute::onUpgrade(
    KiottyNetWebSocketUpgradeAction on_upgrade)
{
    _route = KiottyNet_onWebSocketUpgrade(_route, on_upgrade);
    return *this;
}

inline KiottyNet_WebSocketRoute& KiottyNet_WebSocketRoute::onOpen(
    KiottyNetWebSocketOpenAction on_open)
{
    _route = KiottyNet_onWebSocketOpen(_route, on_open);
    return *this;
}

inline KiottyNet_WebSocketRoute& KiottyNet_WebSocketRoute::onMessage(
    KiottyNetWebSocketMessageAction on_message)
{
    _route = KiottyNet_onWebSocketMessage(_route, on_message);
    return *this;
}

inline KiottyNet_WebSocketRoute& KiottyNet_WebSocketRoute::onClose(
    KiottyNetWebSocketCloseAction on_close)
{
    _route = KiottyNet_onWebSocketClose(_route, on_close);
    return *this;
}

inline KiottyNetCode KiottyNet_WebSocketRoute::build() const
{
    return _route.code;
}

inline KiottyNetWebSocketRoute KiottyNet_WebSocketRoute::route() const
{
    return _route;
}

inline KiottyNet_WebSocketClient::KiottyNet_WebSocketClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration& configuration,
    const KiottyNetHttpLimits* limits,
    const char* path)
    : KiottyNet_WebSocketClient(
          context,
          KiottyNet_createWebSocketClient(context, &configuration, limits, path))
{
}

inline KiottyNet_WebSocketClient::KiottyNet_WebSocketClient(
    KiottyNetContext context,
    KiottyNetWebSocketClientResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline KiottyNet_WebSocketClient::operator bool() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetCode KiottyNet_WebSocketClient::code() const
{
    return _code;
}

inline KiottyNetWebSocketClientHandle KiottyNet_WebSocketClient::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_WebSocketClient::run(
    const KiottyNetAddress& address,
    const KiottyNetWebSocketHandler& handler,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runWebSocketClient(_handle.get(), &address, nullptr, &handler, app_context);
}

inline KiottyNetCode KiottyNet_WebSocketClient::run(
    const KiottyNetAddress& address,
    const KiottyNetTlsClientConfiguration& tls,
    const KiottyNetWebSocketHandler& handler,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runWebSocketClient(_handle.get(), &address, &tls, &handler, app_context);
}

inline void KiottyNet_WebSocketClient::close()
{
    KiottyNet_closeWebSocketClient(_handle.get());
}

inline KiottyNetCode KiottyNet_WebSocketClient::await(uint64_t timeout_milliseconds)
{
    return KiottyNet_awaitWebSocketClient(_handle.get(), timeout_milliseconds);
}

inline KiottyNetWebSocketHandle KiottyNet_WebSocketClient::socket() const
{
    return KiottyNet_getWebSocketClientSocket(_handle.get());
}

inline KiottyNetCode KiottyNet_WebSocketClient::send(
    KiottyNetWebSocketOpcode opcode,
    KiottyNetSpan data)
{
    return KiottyNet_sendWebSocketClient(_handle.get(), opcode, data);
}

#endif

#endif
