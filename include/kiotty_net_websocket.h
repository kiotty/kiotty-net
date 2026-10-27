#ifndef KIOTTY_NET_WEBSOCKET_H
#define KIOTTY_NET_WEBSOCKET_H

#include "kiotty_net_statemachine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIOTTY_MAX_URL_PATH_LENGTH           256
#define KIOTTY_MAX_WEBSOCKET_PROTOCOL_LENGTH 128
#define KIOTTY_MAX_WEBSOCKET_REASON_LENGTH   124

typedef enum KiottyNetWebSocketOpcode
{
    KIOTTY_WS_TEXT   = 1,
    KIOTTY_WS_BINARY = 2
} KiottyNetWebSocketOpcode;

typedef enum KiottyNetWebSocketStatus
{
    KIOTTY_WS_NORMAL_CLOSURE   = 1000,
    KIOTTY_WS_GOING_AWAY       = 1001,
    KIOTTY_WS_PROTOCOL_ERROR   = 1002,
    KIOTTY_WS_UNSUPPORTED_DATA = 1003,
    KIOTTY_WS_NO_STATUS        = 1005,
    KIOTTY_WS_ABNORMAL_CLOSURE = 1006,
    KIOTTY_WS_INVALID_PAYLOAD  = 1007,
    KIOTTY_WS_POLICY_VIOLATION = 1008,
    KIOTTY_WS_MESSAGE_TOO_BIG  = 1009,
    KIOTTY_WS_INTERNAL_ERROR   = 1011
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

typedef struct KiottyNetWebSocketConfig
{
    uint32_t initialization_mark;
    char     path[KIOTTY_MAX_URL_PATH_LENGTH];
    char     protocols[KIOTTY_MAX_WEBSOCKET_PROTOCOL_LENGTH];
    size_t   max_message_bytes;
    uint32_t handshake_timeout_ms;
    uint32_t ping_interval_ms;
    uint32_t pong_timeout_ms;
    uint32_t close_timeout_ms;
} KiottyNetWebSocketConfig;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeWebSocketConfig(KiottyNetWebSocketConfig* config, const char* path);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureWebSocketProtocols(KiottyNetWebSocketConfig* config, const char* protocols);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureWebSocketMaxMessageBytes(KiottyNetWebSocketConfig* config, size_t max_message_bytes);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureWebSocketHandshakeTimeout(KiottyNetWebSocketConfig* config, uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureWebSocketPing(KiottyNetWebSocketConfig* config, uint32_t interval_ms, uint32_t pong_timeout_ms);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureWebSocketCloseTimeout(KiottyNetWebSocketConfig* config, uint32_t timeout_ms);

typedef int64_t KiottyNetWebSocketHandle;
typedef void*   KiottyNetWebSocketContext;

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
    char                    protocol[KIOTTY_MAX_WEBSOCKET_PROTOCOL_LENGTH];
} KiottyNetWebSocketInfo;

typedef struct KiottyNetWebSocketHandler
{
    bool (*onUpgrade)(KiottyNetAppContext app_context, KiottyNetWebSocketHandle ws, KiottyNetWebSocketUpgrade upgrade);
    KiottyNetWebSocketContext (*onOpen)(KiottyNetAppContext app_context, KiottyNetWebSocketHandle ws);
    void (*onMessage)(KiottyNetWebSocketContext ws_context, KiottyNetWebSocketHandle ws, KiottyNetWebSocketMessage message);
    void (*onClose)(KiottyNetAppContext app_context, KiottyNetWebSocketContext ws_context, KiottyNetWebSocketHandle ws, KiottyNetWebSocketClose close);
} KiottyNetWebSocketHandler;

KIOTTY_NET_API KiottyNetWebSocketInfo KiottyNet_getWebSocketInfo(KiottyNetWebSocketHandle ws);
KIOTTY_NET_API KiottyNetSpan          KiottyNet_findWebSocketUpgradeHeader(KiottyNetWebSocketHandle ws, const char* name);
KIOTTY_NET_API KiottyNetCode          KiottyNet_selectWebSocketProtocol(KiottyNetWebSocketHandle ws, const char* protocol);
KIOTTY_NET_API KiottyNetCode          KiottyNet_sendWebSocket(KiottyNetWebSocketHandle ws, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
KIOTTY_NET_API KiottyNetCode          KiottyNet_closeWebSocket(KiottyNetWebSocketHandle ws, uint16_t status, const char* reason);

typedef struct KiottyNetWebSocketServer* KiottyNetWebSocketServerHandle;

KIOTTY_NET_API KiottyNetWebSocketServerHandle KiottyNet_createWebSocketServer(KiottyNetContext context, const KiottyNetServerConfig* config, const KiottyNetTlsServerConfig* tls, const KiottyNetWebSocketConfig* websocket);
KIOTTY_NET_API void                           KiottyNet_destroyWebSocketServer(KiottyNetContext context, KiottyNetWebSocketServerHandle server);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_runWebSocketServer(KiottyNetWebSocketServerHandle server, const KiottyNetWebSocketHandler* handler, KiottyNetAppContext app_context);
KIOTTY_NET_API void                           KiottyNet_closeWebSocketServer(KiottyNetWebSocketServerHandle server);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_awaitWebSocketServer(KiottyNetWebSocketServerHandle server, uint64_t timeout_ms);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_broadcastWebSocketServer(KiottyNetWebSocketServerHandle server, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);

typedef struct KiottyNetWebSocketClient* KiottyNetWebSocketClientHandle;

KIOTTY_NET_API KiottyNetWebSocketClientHandle KiottyNet_createWebSocketClient(KiottyNetContext context, const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls, const KiottyNetWebSocketConfig* websocket);
KIOTTY_NET_API void                           KiottyNet_destroyWebSocketClient(KiottyNetContext context, KiottyNetWebSocketClientHandle client);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_runWebSocketClient(KiottyNetWebSocketClientHandle client, const KiottyNetWebSocketHandler* handler, KiottyNetAppContext app_context);
KIOTTY_NET_API void                           KiottyNet_closeWebSocketClient(KiottyNetWebSocketClientHandle client);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_awaitWebSocketClient(KiottyNetWebSocketClientHandle client, uint64_t timeout_ms);
KIOTTY_NET_API KiottyNetWebSocketHandle       KiottyNet_getWebSocketClientSocket(KiottyNetWebSocketClientHandle client);
KIOTTY_NET_API KiottyNetCode                  KiottyNet_sendWebSocketClient(KiottyNetWebSocketClientHandle client, KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);

#ifdef __cplusplus
}

#ifndef KIOTTY_NET_OWNED_HANDLE_DEFINED
#define KIOTTY_NET_OWNED_HANDLE_DEFINED

namespace kiotty
{
    namespace net
    {
        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        class OwnedHandle
        {
        public:
            OwnedHandle(KiottyNetContext context, Handle handle);
            OwnedHandle(OwnedHandle&& other);
            OwnedHandle& operator=(OwnedHandle&& other);
            ~OwnedHandle();

            OwnedHandle(const OwnedHandle&) = delete;
            OwnedHandle& operator=(const OwnedHandle&) = delete;

            Handle get() const;

        private:
            Handle release();
            void   reset();

            KiottyNetContext _context;
            Handle           _handle;
        };

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>::OwnedHandle(KiottyNetContext context, Handle handle)
            : _context(context)
            , _handle(handle)
        {
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>::OwnedHandle(OwnedHandle&& other)
            : _context(other._context)
            , _handle(other.release())
        {
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>& OwnedHandle<Handle, Destroy>::operator=(OwnedHandle&& other)
        {
            if (this != &other)
            {
                reset();
                _context = other._context;
                _handle  = other.release();
            }
            return *this;
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline OwnedHandle<Handle, Destroy>::~OwnedHandle()
        {
            reset();
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline Handle OwnedHandle<Handle, Destroy>::get() const
        {
            return _handle;
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline Handle OwnedHandle<Handle, Destroy>::release()
        {
            Handle handle = _handle;
            _handle = nullptr;
            return handle;
        }

        template <typename Handle, void (*Destroy)(KiottyNetContext, Handle)>
        inline void OwnedHandle<Handle, Destroy>::reset()
        {
            if (_handle == nullptr)
            {
                return;
            }
            Destroy(_context, _handle);
            _handle = nullptr;
        }
    }
}

#endif

class KiottyNet_WebSocketConfigBuilder
{
public:
    KiottyNet_WebSocketConfigBuilder(KiottyNetWebSocketConfig& config, const char* path);

    KiottyNet_WebSocketConfigBuilder& protocols(const char* protocols);
    KiottyNet_WebSocketConfigBuilder& maxMessageBytes(size_t max_message_bytes);
    KiottyNet_WebSocketConfigBuilder& handshakeTimeout(uint32_t timeout_ms);
    KiottyNet_WebSocketConfigBuilder& ping(uint32_t interval_ms, uint32_t pong_timeout_ms);
    KiottyNet_WebSocketConfigBuilder& closeTimeout(uint32_t timeout_ms);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetWebSocketConfig& _config;
    KiottyNetCode             _code;
};

class KiottyNet_WebSocket
{
public:
    explicit KiottyNet_WebSocket(KiottyNetWebSocketHandle handle);

    KiottyNetWebSocketHandle handle() const;
    KiottyNetWebSocketInfo   info() const;
    KiottyNetSpan            upgradeHeader(const char* name) const;
    KiottyNetCode            selectProtocol(const char* protocol);
    KiottyNetCode            send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);
    KiottyNetCode            close(uint16_t status = KIOTTY_WS_NORMAL_CLOSURE, const char* reason = nullptr);

private:
    KiottyNetWebSocketHandle _handle;
};

class KiottyNet_WebSocketServer
{
public:
    KiottyNet_WebSocketServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetWebSocketConfig& websocket);
    KiottyNet_WebSocketServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls, const KiottyNetWebSocketConfig& websocket);

    bool                           valid() const;
    KiottyNetWebSocketServerHandle handle() const;

    KiottyNetCode run(const KiottyNetWebSocketHandler& handler, KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
    KiottyNetCode broadcast(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);

private:
    kiotty::net::OwnedHandle<KiottyNetWebSocketServerHandle, KiottyNet_destroyWebSocketServer> _handle;
};

class KiottyNet_WebSocketClient
{
public:
    KiottyNet_WebSocketClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetWebSocketConfig& websocket);
    KiottyNet_WebSocketClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls, const KiottyNetWebSocketConfig& websocket);

    bool                           valid() const;
    KiottyNetWebSocketClientHandle handle() const;

    KiottyNetCode            run(const KiottyNetWebSocketHandler& handler, KiottyNetAppContext app_context);
    void                     close();
    KiottyNetCode            await(uint64_t timeout_ms);
    KiottyNetWebSocketHandle socket() const;
    KiottyNetCode            send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data);

private:
    kiotty::net::OwnedHandle<KiottyNetWebSocketClientHandle, KiottyNet_destroyWebSocketClient> _handle;
};

inline KiottyNet_WebSocketConfigBuilder::KiottyNet_WebSocketConfigBuilder(KiottyNetWebSocketConfig& config, const char* path)
    : _config(config)
    , _code(KiottyNet_initializeWebSocketConfig(&config, path))
{
}

inline KiottyNet_WebSocketConfigBuilder& KiottyNet_WebSocketConfigBuilder::protocols(const char* protocols)
{
    record(KiottyNet_configureWebSocketProtocols(&_config, protocols));
    return *this;
}

inline KiottyNet_WebSocketConfigBuilder& KiottyNet_WebSocketConfigBuilder::maxMessageBytes(size_t max_message_bytes)
{
    record(KiottyNet_configureWebSocketMaxMessageBytes(&_config, max_message_bytes));
    return *this;
}

inline KiottyNet_WebSocketConfigBuilder& KiottyNet_WebSocketConfigBuilder::handshakeTimeout(uint32_t timeout_ms)
{
    record(KiottyNet_configureWebSocketHandshakeTimeout(&_config, timeout_ms));
    return *this;
}

inline KiottyNet_WebSocketConfigBuilder& KiottyNet_WebSocketConfigBuilder::ping(uint32_t interval_ms, uint32_t pong_timeout_ms)
{
    record(KiottyNet_configureWebSocketPing(&_config, interval_ms, pong_timeout_ms));
    return *this;
}

inline KiottyNet_WebSocketConfigBuilder& KiottyNet_WebSocketConfigBuilder::closeTimeout(uint32_t timeout_ms)
{
    record(KiottyNet_configureWebSocketCloseTimeout(&_config, timeout_ms));
    return *this;
}

inline KiottyNetCode KiottyNet_WebSocketConfigBuilder::build() const
{
    return _code;
}

inline void KiottyNet_WebSocketConfigBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_SUCCESS)
    {
        _code = code;
    }
}

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

inline KiottyNet_WebSocketServer::KiottyNet_WebSocketServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetWebSocketConfig& websocket)
    : _handle(context, KiottyNet_createWebSocketServer(context, &config, nullptr, &websocket))
{
}

inline KiottyNet_WebSocketServer::KiottyNet_WebSocketServer(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls, const KiottyNetWebSocketConfig& websocket)
    : _handle(context, KiottyNet_createWebSocketServer(context, &config, &tls, &websocket))
{
}

inline bool KiottyNet_WebSocketServer::valid() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetWebSocketServerHandle KiottyNet_WebSocketServer::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_WebSocketServer::run(const KiottyNetWebSocketHandler& handler, KiottyNetAppContext app_context)
{
    return KiottyNet_runWebSocketServer(_handle.get(), &handler, app_context);
}

inline void KiottyNet_WebSocketServer::close()
{
    KiottyNet_closeWebSocketServer(_handle.get());
}

inline KiottyNetCode KiottyNet_WebSocketServer::await(uint64_t timeout_ms)
{
    return KiottyNet_awaitWebSocketServer(_handle.get(), timeout_ms);
}

inline KiottyNetCode KiottyNet_WebSocketServer::broadcast(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data)
{
    return KiottyNet_broadcastWebSocketServer(_handle.get(), opcode, data);
}

inline KiottyNet_WebSocketClient::KiottyNet_WebSocketClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetWebSocketConfig& websocket)
    : _handle(context, KiottyNet_createWebSocketClient(context, &config, nullptr, &websocket))
{
}

inline KiottyNet_WebSocketClient::KiottyNet_WebSocketClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls, const KiottyNetWebSocketConfig& websocket)
    : _handle(context, KiottyNet_createWebSocketClient(context, &config, &tls, &websocket))
{
}

inline bool KiottyNet_WebSocketClient::valid() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetWebSocketClientHandle KiottyNet_WebSocketClient::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_WebSocketClient::run(const KiottyNetWebSocketHandler& handler, KiottyNetAppContext app_context)
{
    return KiottyNet_runWebSocketClient(_handle.get(), &handler, app_context);
}

inline void KiottyNet_WebSocketClient::close()
{
    KiottyNet_closeWebSocketClient(_handle.get());
}

inline KiottyNetCode KiottyNet_WebSocketClient::await(uint64_t timeout_ms)
{
    return KiottyNet_awaitWebSocketClient(_handle.get(), timeout_ms);
}

inline KiottyNetWebSocketHandle KiottyNet_WebSocketClient::socket() const
{
    return KiottyNet_getWebSocketClientSocket(_handle.get());
}

inline KiottyNetCode KiottyNet_WebSocketClient::send(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data)
{
    return KiottyNet_sendWebSocketClient(_handle.get(), opcode, data);
}

#endif

#endif
