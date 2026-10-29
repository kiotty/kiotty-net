#ifndef KIOTTY_NET_BASE_H
#define KIOTTY_NET_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(KIOTTY_NET_SHARED) && defined(_WIN32)
#  if defined(KIOTTY_NET_BUILD)
#    define KIOTTY_NET_API __declspec(dllexport)
#  else
#    define KIOTTY_NET_API __declspec(dllimport)
#  endif
#elif defined(KIOTTY_NET_SHARED)
#  define KIOTTY_NET_API __attribute__((visibility("default")))
#else
#  define KIOTTY_NET_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KIOTTY_MAX_HOST_LENGTH     256
#define KIOTTY_MAX_PATH_LENGTH     260
#define KIOTTY_MAX_PASSWORD_LENGTH 128

typedef enum KiottyNetCode
{
    KIOTTY_SUCCESS,
    KIOTTY_PEER_CLOSED,
    KIOTTY_SOCKET_ERROR,
    KIOTTY_PROTOCOL_ERROR,
    KIOTTY_REQUEST_TOO_LARGE,
    KIOTTY_UNHANDLED_EVENT,
    KIOTTY_ILLEGAL_TRANSITION,
    KIOTTY_SESSION_EXHAUSTED,
    KIOTTY_DUPLICATE_RULE,
    KIOTTY_TIMEOUT,
    KIOTTY_TABLE_FULL,
    KIOTTY_BUFFER_FULL,
    KIOTTY_NOT_FOUND,
    KIOTTY_INVALID_ARGUMENT,
    KIOTTY_CLOSED,
    KIOTTY_BUSY,
    KIOTTY_TLS_NOT_SUPPORTED,
    KIOTTY_TLS_CERTIFICATE_NOT_FOUND,
    KIOTTY_TLS_CERTIFICATE_INVALID,
    KIOTTY_TLS_VERIFY_FAILED,
    KIOTTY_TLS_HOSTNAME_MISMATCH,
    KIOTTY_TLS_HANDSHAKE_FAILED,
    KIOTTY_TLS_HANDSHAKE_TIMEOUT,
    KIOTTY_TLS_RECORD_ERROR
} KiottyNetCode;

typedef struct KiottyNetResult
{
    int32_t       bytes;
    KiottyNetCode code;
} KiottyNetResult;

typedef struct KiottyNetSpan
{
    const char* data;
    size_t      length;
} KiottyNetSpan;

typedef enum KiottyNetTlsVersion
{
    KIOTTY_TLS_V12 = 1 << 0,
    KIOTTY_TLS_V13 = 1 << 1
} KiottyNetTlsVersion;

typedef enum KiottyNetTlsV12CipherSuite
{
    KIOTTY_TLS12_ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
    KIOTTY_TLS12_ECDHE_RSA_AES256_GCM_SHA384   = 1 << 1,
    KIOTTY_TLS12_ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2,
    KIOTTY_TLS12_ECDHE_RSA_CHACHA20_POLY1305   = 1 << 3
} KiottyNetTlsV12CipherSuite;

typedef enum KiottyNetTlsV13CipherSuite
{
    KIOTTY_TLS13_AES_128_GCM_SHA256       = 1 << 0,
    KIOTTY_TLS13_AES_256_GCM_SHA384       = 1 << 1,
    KIOTTY_TLS13_CHACHA20_POLY1305_SHA256 = 1 << 2,
    KIOTTY_TLS13_AES_128_CCM_SHA256       = 1 << 3,
    KIOTTY_TLS13_AES_128_CCM8_SHA256      = 1 << 4
} KiottyNetTlsV13CipherSuite;

typedef enum KiottyNetClientAuth
{
    KIOTTY_CLIENT_AUTH_NONE,
    KIOTTY_CLIENT_AUTH_OPTIONAL,
    KIOTTY_CLIENT_AUTH_REQUIRED
} KiottyNetClientAuth;

typedef struct KiottyNetTlsIdentity
{
    char cert_file[KIOTTY_MAX_PATH_LENGTH];
    char key_file[KIOTTY_MAX_PATH_LENGTH];
    char key_password[KIOTTY_MAX_PASSWORD_LENGTH];
} KiottyNetTlsIdentity;

typedef struct KiottyNetTlsServerConfig
{
    uint32_t             initialization_mark;
    uint32_t             versions;
    uint32_t             v12_cipher_suites;
    uint32_t             v13_cipher_suites;
    KiottyNetTlsIdentity identity;
    KiottyNetClientAuth  client_auth;
    char                 trust_store_path[KIOTTY_MAX_PATH_LENGTH];
} KiottyNetTlsServerConfig;

typedef struct KiottyNetTlsClientConfig
{
    uint32_t             initialization_mark;
    uint32_t             versions;
    uint32_t             v12_cipher_suites;
    uint32_t             v13_cipher_suites;
    int                  skip_server_verification;
    int                  skip_hostname_verification;
    char                 trust_store_path[KIOTTY_MAX_PATH_LENGTH];
    KiottyNetTlsIdentity identity;
} KiottyNetTlsClientConfig;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTlsServerConfig(KiottyNetTlsServerConfig* config, const char* cert_file, const char* key_file, const char* key_password);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerVersions(KiottyNetTlsServerConfig* config, bool tls_1_2, bool tls_1_3);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerV12CipherSuites(KiottyNetTlsServerConfig* config, uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerV13CipherSuites(KiottyNetTlsServerConfig* config, uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerClientAuth(KiottyNetTlsServerConfig* config, KiottyNetClientAuth mode, const char* trust_store_path);

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTlsClientConfig(KiottyNetTlsClientConfig* config);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientVersions(KiottyNetTlsClientConfig* config, bool tls_1_2, bool tls_1_3);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientV12CipherSuites(KiottyNetTlsClientConfig* config, uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientV13CipherSuites(KiottyNetTlsClientConfig* config, uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientTrustStore(KiottyNetTlsClientConfig* config, const char* trust_store_path);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientVerification(KiottyNetTlsClientConfig* config, bool verify_server, bool verify_hostname);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientIdentity(KiottyNetTlsClientConfig* config, const char* cert_file, const char* key_file, const char* key_password);

typedef enum KiottyNetTransport
{
    KIOTTY_TCP,
    KIOTTY_PIPE
} KiottyNetTransport;

typedef struct KiottyNetEndpoint
{
    KiottyNetTransport transport;
    char               host[KIOTTY_MAX_HOST_LENGTH];
    uint16_t           port;
} KiottyNetEndpoint;

typedef struct KiottyNetServerConfig
{
    uint32_t          initialization_mark;
    KiottyNetEndpoint endpoint;
    size_t            max_clients;
    size_t            recv_capacity;
    size_t            send_capacity;
    size_t            push_capacity;
    bool              no_delay;
} KiottyNetServerConfig;

typedef struct KiottyNetClientConfig
{
    uint32_t          initialization_mark;
    KiottyNetEndpoint endpoint;
    size_t            recv_capacity;
    size_t            send_capacity;
    size_t            push_capacity;
    uint32_t          connect_timeout_ms;
    uint32_t          max_trial;
    uint32_t          trial_interval_ms;
    bool              no_delay;
} KiottyNetClientConfig;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTcpServerConfig(KiottyNetServerConfig* config, const char* host, uint16_t port);
KIOTTY_NET_API KiottyNetCode KiottyNet_initializePipeServerConfig(KiottyNetServerConfig* config, const char* pipe_name);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerMaxClients(KiottyNetServerConfig* config, size_t max_clients);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerBufferCapacity(KiottyNetServerConfig* config, size_t recv_capacity, size_t send_capacity, size_t push_capacity);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerNoDelay(KiottyNetServerConfig* config, bool no_delay);

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTcpClientConfig(KiottyNetClientConfig* config, const char* host, uint16_t port);
KIOTTY_NET_API KiottyNetCode KiottyNet_initializePipeClientConfig(KiottyNetClientConfig* config, const char* pipe_name);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientBufferCapacity(KiottyNetClientConfig* config, size_t recv_capacity, size_t send_capacity, size_t push_capacity);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientConnectTimeout(KiottyNetClientConfig* config, uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientRetry(KiottyNetClientConfig* config, uint32_t max_trial, uint32_t trial_interval_ms);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientNoDelay(KiottyNetClientConfig* config, bool no_delay);

typedef int64_t KiottyNetConnectionHandle;
typedef int64_t KiottyNetWorkHandle;
typedef void*   KiottyNetAppContext;
typedef void*   KiottyNetConnectionContext;

typedef struct KiottyNetConnectionInfo
{
    char    hostname[KIOTTY_MAX_HOST_LENGTH];
    int32_t port;
} KiottyNetConnectionInfo;

typedef enum KiottyNetActionType
{
    KIOTTY_ACTION_HOLD,
    KIOTTY_ACTION_READ,
    KIOTTY_ACTION_READ_UNTIL,
    KIOTTY_ACTION_WRITE,
    KIOTTY_ACTION_START_WORK,
    KIOTTY_ACTION_CLOSE
} KiottyNetActionType;

typedef struct KiottyNetConnectionAction
{
    KiottyNetActionType type;
    uint32_t            timeout_ms;
    union
    {
        struct
        {
            size_t exact;
        } read;
        struct
        {
            KiottyNetSpan delimiter;
            size_t        max;
        } read_until;
        struct
        {
            KiottyNetSpan data;
        } write;
        struct
        {
            KiottyNetCode reason;
        } close;
    } as;
} KiottyNetConnectionAction;

KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_holdCommunication(uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_readConnection(size_t exact, uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_readConnectionUntil(KiottyNetSpan delimiter, size_t max, uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_writeConnection(KiottyNetSpan data, uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_startWork(void);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_closeConnection(KiottyNetCode reason);

typedef struct KiottyNetConnectionDecision
{
    bool                       accepted;
    KiottyNetConnectionContext connection_context;
} KiottyNetConnectionDecision;

KIOTTY_NET_API KiottyNetConnectionDecision KiottyNet_acceptConnection(KiottyNetConnectionContext connection_context);
KIOTTY_NET_API KiottyNetConnectionDecision KiottyNet_ignoreConnection(void);

typedef struct KiottyNetConnectionHandler
{
    KiottyNetConnectionDecision (*onConnected)(KiottyNetAppContext app_context, KiottyNetConnectionInfo info);
    KiottyNetConnectionAction (*onReady)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection);
    KiottyNetConnectionAction (*onRead)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, KiottyNetSpan data);
    KiottyNetConnectionAction (*onWritten)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, size_t length);
    KiottyNetConnectionAction (*onTimeout)(KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection);
    void (*onWorkStarted)(KiottyNetConnectionContext connection_context, KiottyNetWorkHandle work);
    void (*onDisconnected)(KiottyNetAppContext app_context, KiottyNetConnectionContext connection_context, KiottyNetConnectionHandle connection, KiottyNetCode reason);
    void (*onError)(KiottyNetAppContext app_context, KiottyNetConnectionHandle connection, KiottyNetCode code, const char* message);
} KiottyNetConnectionHandler;

KIOTTY_NET_API KiottyNetConnectionInfo KiottyNet_getConnectionInfo(KiottyNetConnectionHandle connection);
KIOTTY_NET_API KiottyNetCode           KiottyNet_notifyConnection(KiottyNetConnectionHandle connection, KiottyNetSpan data);

KIOTTY_NET_API KiottyNetCode KiottyNet_finishWork(KiottyNetWorkHandle work, KiottyNetConnectionAction action);
KIOTTY_NET_API bool          KiottyNet_isWorkConnected(KiottyNetWorkHandle work);

typedef struct KiottyNetContextImpl* KiottyNetContext;

typedef struct KiottyNetContextResult
{
    KiottyNetCode    code;
    KiottyNetContext handle;
} KiottyNetContextResult;

KIOTTY_NET_API KiottyNetContextResult KiottyNet_initializeContext(void);
KIOTTY_NET_API void                   KiottyNet_releaseContext(KiottyNetContext context);

typedef struct KiottyNetStreamClient* KiottyNetStreamClientHandle;

typedef struct KiottyNetStreamClientResult
{
    KiottyNetCode               code;
    KiottyNetStreamClientHandle handle;
} KiottyNetStreamClientResult;

KIOTTY_NET_API KiottyNetStreamClientResult KiottyNet_createStreamClient(KiottyNetContext context, const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls);
KIOTTY_NET_API void                        KiottyNet_destroyStreamClient(KiottyNetContext context, KiottyNetStreamClientHandle client);
KIOTTY_NET_API KiottyNetCode               KiottyNet_connectStreamClient(KiottyNetStreamClientHandle client);
KIOTTY_NET_API void                        KiottyNet_disconnectStreamClient(KiottyNetStreamClientHandle client);
KIOTTY_NET_API KiottyNetResult             KiottyNet_readStreamClient(KiottyNetStreamClientHandle client, void* buffer, size_t length, uint32_t timeout_ms);
KIOTTY_NET_API KiottyNetResult             KiottyNet_writeStreamClient(KiottyNetStreamClientHandle client, KiottyNetSpan data, uint32_t timeout_ms);

typedef struct KiottyNetEventClient* KiottyNetEventClientHandle;

typedef struct KiottyNetEventClientResult
{
    KiottyNetCode              code;
    KiottyNetEventClientHandle handle;
} KiottyNetEventClientResult;

KIOTTY_NET_API KiottyNetEventClientResult KiottyNet_createEventClient(KiottyNetContext context, const KiottyNetClientConfig* config, const KiottyNetTlsClientConfig* tls);
KIOTTY_NET_API void                       KiottyNet_destroyEventClient(KiottyNetContext context, KiottyNetEventClientHandle client);
KIOTTY_NET_API KiottyNetCode              KiottyNet_runEventClient(KiottyNetEventClientHandle client, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);
KIOTTY_NET_API void                       KiottyNet_closeEventClient(KiottyNetEventClientHandle client);
KIOTTY_NET_API KiottyNetCode              KiottyNet_awaitEventClient(KiottyNetEventClientHandle client, uint64_t timeout_ms);
KIOTTY_NET_API KiottyNetConnectionHandle  KiottyNet_getEventClientConnection(KiottyNetEventClientHandle client);
KIOTTY_NET_API KiottyNetCode              KiottyNet_notifyEventClient(KiottyNetEventClientHandle client, KiottyNetSpan data);

typedef struct KiottyNetServer* KiottyNetServerHandle;

typedef struct KiottyNetServerResult
{
    KiottyNetCode         code;
    KiottyNetServerHandle handle;
} KiottyNetServerResult;

KIOTTY_NET_API KiottyNetServerResult KiottyNet_createServer(KiottyNetContext context, const KiottyNetServerConfig* config, const KiottyNetTlsServerConfig* tls);
KIOTTY_NET_API void                  KiottyNet_destroyServer(KiottyNetContext context, KiottyNetServerHandle server);
KIOTTY_NET_API KiottyNetCode         KiottyNet_runServer(KiottyNetServerHandle server, const KiottyNetConnectionHandler* handler, KiottyNetAppContext app_context);
KIOTTY_NET_API void                  KiottyNet_closeServer(KiottyNetServerHandle server);
KIOTTY_NET_API KiottyNetCode         KiottyNet_awaitServer(KiottyNetServerHandle server, uint64_t timeout_ms);
KIOTTY_NET_API KiottyNetCode         KiottyNet_broadcastServer(KiottyNetServerHandle server, KiottyNetSpan data);

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

class KiottyNet_ServerConfigBuilder
{
public:
    static KiottyNet_ServerConfigBuilder tcp(KiottyNetServerConfig& config, const char* host, uint16_t port);
    static KiottyNet_ServerConfigBuilder pipe(KiottyNetServerConfig& config, const char* pipe_name);

    KiottyNet_ServerConfigBuilder& maxClients(size_t max_clients);
    KiottyNet_ServerConfigBuilder& bufferCapacity(size_t recv_capacity, size_t send_capacity, size_t push_capacity);
    KiottyNet_ServerConfigBuilder& noDelay(bool no_delay);

    KiottyNetCode build() const;

private:
    KiottyNet_ServerConfigBuilder(KiottyNetServerConfig& config, KiottyNetCode initialized);

    void record(KiottyNetCode code);

    KiottyNetServerConfig&    _config;
    KiottyNetCode             _code;
};

class KiottyNet_ClientConfigBuilder
{
public:
    static KiottyNet_ClientConfigBuilder tcp(KiottyNetClientConfig& config, const char* host, uint16_t port);
    static KiottyNet_ClientConfigBuilder pipe(KiottyNetClientConfig& config, const char* pipe_name);

    KiottyNet_ClientConfigBuilder& bufferCapacity(size_t recv_capacity, size_t send_capacity, size_t push_capacity);
    KiottyNet_ClientConfigBuilder& connectTimeout(uint32_t timeout_ms);
    KiottyNet_ClientConfigBuilder& retry(uint32_t max_trial, uint32_t trial_interval_ms);
    KiottyNet_ClientConfigBuilder& noDelay(bool no_delay);

    KiottyNetCode build() const;

private:
    KiottyNet_ClientConfigBuilder(KiottyNetClientConfig& config, KiottyNetCode initialized);

    void record(KiottyNetCode code);

    KiottyNetClientConfig&    _config;
    KiottyNetCode             _code;
};

class KiottyNet_TlsServerConfigBuilder
{
public:
    KiottyNet_TlsServerConfigBuilder(KiottyNetTlsServerConfig& config, const char* cert_file, const char* key_file, const char* key_password = nullptr);

    KiottyNet_TlsServerConfigBuilder& versions(bool tls_1_2, bool tls_1_3);
    KiottyNet_TlsServerConfigBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigBuilder& clientAuth(KiottyNetClientAuth mode, const char* trust_store_path);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetTlsServerConfig& _config;
    KiottyNetCode             _code;
};

class KiottyNet_TlsClientConfigBuilder
{
public:
    explicit KiottyNet_TlsClientConfigBuilder(KiottyNetTlsClientConfig& config);

    KiottyNet_TlsClientConfigBuilder& versions(bool tls_1_2, bool tls_1_3);
    KiottyNet_TlsClientConfigBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigBuilder& trustStore(const char* trust_store_path);
    KiottyNet_TlsClientConfigBuilder& verification(bool verify_server, bool verify_hostname);
    KiottyNet_TlsClientConfigBuilder& identity(const char* cert_file, const char* key_file, const char* key_password = nullptr);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetTlsClientConfig& _config;
    KiottyNetCode             _code;
};

class KiottyNet_Connection
{
public:
    explicit KiottyNet_Connection(KiottyNetConnectionHandle handle);

    KiottyNetConnectionHandle handle() const;
    KiottyNetConnectionInfo   info() const;
    KiottyNetCode             notify(KiottyNetSpan data);

private:
    KiottyNetConnectionHandle _handle;
};

class KiottyNet_Work
{
public:
    explicit KiottyNet_Work(KiottyNetWorkHandle work);
    KiottyNet_Work(KiottyNet_Work&& other);
    KiottyNet_Work& operator=(KiottyNet_Work&& other);
    ~KiottyNet_Work();

    KiottyNet_Work(const KiottyNet_Work&) = delete;
    KiottyNet_Work& operator=(const KiottyNet_Work&) = delete;

    bool          connected() const;
    KiottyNetCode finish(KiottyNetConnectionAction action);

private:
    KiottyNetWorkHandle release();
    void                reset();

    KiottyNetWorkHandle _work;
};

class KiottyNet_Context
{
public:
    KiottyNet_Context();
    KiottyNet_Context(KiottyNet_Context&& other);
    KiottyNet_Context& operator=(KiottyNet_Context&& other);
    ~KiottyNet_Context();

    KiottyNet_Context(const KiottyNet_Context&) = delete;
    KiottyNet_Context& operator=(const KiottyNet_Context&) = delete;

    bool             valid() const;
    KiottyNetCode    code() const;
    KiottyNetContext handle() const;

private:
    explicit KiottyNet_Context(KiottyNetContextResult result);

    KiottyNetContext release();
    void             reset();

    KiottyNetCode    _code;
    KiottyNetContext _context;
};

class KiottyNet_StreamClient
{
public:
    KiottyNet_StreamClient(KiottyNetContext context, const KiottyNetClientConfig& config);
    KiottyNet_StreamClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls);

    bool                        valid() const;
    KiottyNetCode               code() const;
    KiottyNetStreamClientHandle handle() const;

    KiottyNetCode   connect();
    void            disconnect();
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_ms);
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_ms);

private:
    KiottyNet_StreamClient(KiottyNetContext context, KiottyNetStreamClientResult result);

    KiottyNetCode _code;
    kiotty::net::OwnedHandle<KiottyNetStreamClientHandle, KiottyNet_destroyStreamClient> _handle;
};

class KiottyNet_EventClient
{
public:
    KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfig& config);
    KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls);

    bool                       valid() const;
    KiottyNetCode              code() const;
    KiottyNetEventClientHandle handle() const;

    KiottyNetCode             run(const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);
    void                      close();
    KiottyNetCode             await(uint64_t timeout_ms);
    KiottyNetConnectionHandle connection() const;
    KiottyNetCode             notify(KiottyNetSpan data);

private:
    KiottyNet_EventClient(KiottyNetContext context, KiottyNetEventClientResult result);

    KiottyNetCode _code;
    kiotty::net::OwnedHandle<KiottyNetEventClientHandle, KiottyNet_destroyEventClient> _handle;
};

class KiottyNet_Server
{
public:
    KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfig& config);
    KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls);

    bool                  valid() const;
    KiottyNetCode         code() const;
    KiottyNetServerHandle handle() const;

    KiottyNetCode run(const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_ms);
    KiottyNetCode broadcast(KiottyNetSpan data);

private:
    KiottyNet_Server(KiottyNetContext context, KiottyNetServerResult result);

    KiottyNetCode _code;
    kiotty::net::OwnedHandle<KiottyNetServerHandle, KiottyNet_destroyServer> _handle;
};

inline KiottyNet_ServerConfigBuilder::KiottyNet_ServerConfigBuilder(KiottyNetServerConfig& config, KiottyNetCode initialized)
    : _config(config)
    , _code(initialized)
{
}

inline KiottyNet_ServerConfigBuilder KiottyNet_ServerConfigBuilder::tcp(KiottyNetServerConfig& config, const char* host, uint16_t port)
{
    return KiottyNet_ServerConfigBuilder(config, KiottyNet_initializeTcpServerConfig(&config, host, port));
}

inline KiottyNet_ServerConfigBuilder KiottyNet_ServerConfigBuilder::pipe(KiottyNetServerConfig& config, const char* pipe_name)
{
    return KiottyNet_ServerConfigBuilder(config, KiottyNet_initializePipeServerConfig(&config, pipe_name));
}

inline KiottyNet_ServerConfigBuilder& KiottyNet_ServerConfigBuilder::maxClients(size_t max_clients)
{
    record(KiottyNet_configureServerMaxClients(&_config, max_clients));
    return *this;
}

inline KiottyNet_ServerConfigBuilder& KiottyNet_ServerConfigBuilder::bufferCapacity(size_t recv_capacity, size_t send_capacity, size_t push_capacity)
{
    record(KiottyNet_configureServerBufferCapacity(&_config, recv_capacity, send_capacity, push_capacity));
    return *this;
}

inline KiottyNet_ServerConfigBuilder& KiottyNet_ServerConfigBuilder::noDelay(bool no_delay)
{
    record(KiottyNet_configureServerNoDelay(&_config, no_delay));
    return *this;
}

inline KiottyNetCode KiottyNet_ServerConfigBuilder::build() const
{
    return _code;
}

inline void KiottyNet_ServerConfigBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_ClientConfigBuilder::KiottyNet_ClientConfigBuilder(KiottyNetClientConfig& config, KiottyNetCode initialized)
    : _config(config)
    , _code(initialized)
{
}

inline KiottyNet_ClientConfigBuilder KiottyNet_ClientConfigBuilder::tcp(KiottyNetClientConfig& config, const char* host, uint16_t port)
{
    return KiottyNet_ClientConfigBuilder(config, KiottyNet_initializeTcpClientConfig(&config, host, port));
}

inline KiottyNet_ClientConfigBuilder KiottyNet_ClientConfigBuilder::pipe(KiottyNetClientConfig& config, const char* pipe_name)
{
    return KiottyNet_ClientConfigBuilder(config, KiottyNet_initializePipeClientConfig(&config, pipe_name));
}

inline KiottyNet_ClientConfigBuilder& KiottyNet_ClientConfigBuilder::bufferCapacity(size_t recv_capacity, size_t send_capacity, size_t push_capacity)
{
    record(KiottyNet_configureClientBufferCapacity(&_config, recv_capacity, send_capacity, push_capacity));
    return *this;
}

inline KiottyNet_ClientConfigBuilder& KiottyNet_ClientConfigBuilder::connectTimeout(uint32_t timeout_ms)
{
    record(KiottyNet_configureClientConnectTimeout(&_config, timeout_ms));
    return *this;
}

inline KiottyNet_ClientConfigBuilder& KiottyNet_ClientConfigBuilder::retry(uint32_t max_trial, uint32_t trial_interval_ms)
{
    record(KiottyNet_configureClientRetry(&_config, max_trial, trial_interval_ms));
    return *this;
}

inline KiottyNet_ClientConfigBuilder& KiottyNet_ClientConfigBuilder::noDelay(bool no_delay)
{
    record(KiottyNet_configureClientNoDelay(&_config, no_delay));
    return *this;
}

inline KiottyNetCode KiottyNet_ClientConfigBuilder::build() const
{
    return _code;
}

inline void KiottyNet_ClientConfigBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_TlsServerConfigBuilder::KiottyNet_TlsServerConfigBuilder(KiottyNetTlsServerConfig& config, const char* cert_file, const char* key_file, const char* key_password)
    : _config(config)
    , _code(KiottyNet_initializeTlsServerConfig(&config, cert_file, key_file, key_password))
{
}

inline KiottyNet_TlsServerConfigBuilder& KiottyNet_TlsServerConfigBuilder::versions(bool tls_1_2, bool tls_1_3)
{
    record(KiottyNet_configureTlsServerVersions(&_config, tls_1_2, tls_1_3));
    return *this;
}

inline KiottyNet_TlsServerConfigBuilder& KiottyNet_TlsServerConfigBuilder::v12CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsServerV12CipherSuites(&_config, suites));
    return *this;
}

inline KiottyNet_TlsServerConfigBuilder& KiottyNet_TlsServerConfigBuilder::v13CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsServerV13CipherSuites(&_config, suites));
    return *this;
}

inline KiottyNet_TlsServerConfigBuilder& KiottyNet_TlsServerConfigBuilder::clientAuth(KiottyNetClientAuth mode, const char* trust_store_path)
{
    record(KiottyNet_configureTlsServerClientAuth(&_config, mode, trust_store_path));
    return *this;
}

inline KiottyNetCode KiottyNet_TlsServerConfigBuilder::build() const
{
    return _code;
}

inline void KiottyNet_TlsServerConfigBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_TlsClientConfigBuilder::KiottyNet_TlsClientConfigBuilder(KiottyNetTlsClientConfig& config)
    : _config(config)
    , _code(KiottyNet_initializeTlsClientConfig(&config))
{
}

inline KiottyNet_TlsClientConfigBuilder& KiottyNet_TlsClientConfigBuilder::versions(bool tls_1_2, bool tls_1_3)
{
    record(KiottyNet_configureTlsClientVersions(&_config, tls_1_2, tls_1_3));
    return *this;
}

inline KiottyNet_TlsClientConfigBuilder& KiottyNet_TlsClientConfigBuilder::v12CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsClientV12CipherSuites(&_config, suites));
    return *this;
}

inline KiottyNet_TlsClientConfigBuilder& KiottyNet_TlsClientConfigBuilder::v13CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsClientV13CipherSuites(&_config, suites));
    return *this;
}

inline KiottyNet_TlsClientConfigBuilder& KiottyNet_TlsClientConfigBuilder::trustStore(const char* trust_store_path)
{
    record(KiottyNet_configureTlsClientTrustStore(&_config, trust_store_path));
    return *this;
}

inline KiottyNet_TlsClientConfigBuilder& KiottyNet_TlsClientConfigBuilder::verification(bool verify_server, bool verify_hostname)
{
    record(KiottyNet_configureTlsClientVerification(&_config, verify_server, verify_hostname));
    return *this;
}

inline KiottyNet_TlsClientConfigBuilder& KiottyNet_TlsClientConfigBuilder::identity(const char* cert_file, const char* key_file, const char* key_password)
{
    record(KiottyNet_configureTlsClientIdentity(&_config, cert_file, key_file, key_password));
    return *this;
}

inline KiottyNetCode KiottyNet_TlsClientConfigBuilder::build() const
{
    return _code;
}

inline void KiottyNet_TlsClientConfigBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_Connection::KiottyNet_Connection(KiottyNetConnectionHandle handle)
    : _handle(handle)
{
}

inline KiottyNetConnectionHandle KiottyNet_Connection::handle() const
{
    return _handle;
}

inline KiottyNetConnectionInfo KiottyNet_Connection::info() const
{
    return KiottyNet_getConnectionInfo(_handle);
}

inline KiottyNetCode KiottyNet_Connection::notify(KiottyNetSpan data)
{
    return KiottyNet_notifyConnection(_handle, data);
}

inline KiottyNet_Work::KiottyNet_Work(KiottyNetWorkHandle work)
    : _work(work)
{
}

inline KiottyNet_Work::KiottyNet_Work(KiottyNet_Work&& other)
    : _work(other.release())
{
}

inline KiottyNet_Work& KiottyNet_Work::operator=(KiottyNet_Work&& other)
{
    if (this != &other)
    {
        reset();
        _work = other.release();
    }
    return *this;
}

inline KiottyNet_Work::~KiottyNet_Work()
{
    reset();
}

inline bool KiottyNet_Work::connected() const
{
    return KiottyNet_isWorkConnected(_work);
}

inline KiottyNetCode KiottyNet_Work::finish(KiottyNetConnectionAction action)
{
    KiottyNetCode code = KiottyNet_finishWork(_work, action);
    if (code == KIOTTY_BUFFER_FULL || code == KIOTTY_INVALID_ARGUMENT)
    {
        return code;
    }
    release();
    return code;
}

inline KiottyNetWorkHandle KiottyNet_Work::release()
{
    KiottyNetWorkHandle work = _work;
    _work = 0;
    return work;
}

inline void KiottyNet_Work::reset()
{
    if (_work == 0)
    {
        return;
    }
    KiottyNetWorkHandle work = release();
    KiottyNetConnectionAction close = KiottyNet_closeConnection(KIOTTY_SUCCESS);
    KiottyNet_finishWork(work, close);
}

inline KiottyNet_Context::KiottyNet_Context()
    : KiottyNet_Context(KiottyNet_initializeContext())
{
}

inline KiottyNet_Context::KiottyNet_Context(KiottyNetContextResult result)
    : _code(result.code)
    , _context(result.handle)
{
}

inline KiottyNet_Context::KiottyNet_Context(KiottyNet_Context&& other)
    : _code(other._code)
    , _context(other.release())
{
}

inline KiottyNet_Context& KiottyNet_Context::operator=(KiottyNet_Context&& other)
{
    if (this != &other)
    {
        reset();
        _code    = other._code;
        _context = other.release();
    }
    return *this;
}

inline KiottyNet_Context::~KiottyNet_Context()
{
    reset();
}

inline bool KiottyNet_Context::valid() const
{
    return _context != nullptr;
}

inline KiottyNetCode KiottyNet_Context::code() const
{
    return _code;
}

inline KiottyNetContext KiottyNet_Context::handle() const
{
    return _context;
}

inline KiottyNetContext KiottyNet_Context::release()
{
    KiottyNetContext context = _context;
    _context = nullptr;
    return context;
}

inline void KiottyNet_Context::reset()
{
    if (_context == nullptr)
    {
        return;
    }
    KiottyNet_releaseContext(_context);
    _context = nullptr;
}

inline KiottyNet_StreamClient::KiottyNet_StreamClient(KiottyNetContext context, const KiottyNetClientConfig& config)
    : KiottyNet_StreamClient(context, KiottyNet_createStreamClient(context, &config, nullptr))
{
}

inline KiottyNet_StreamClient::KiottyNet_StreamClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls)
    : KiottyNet_StreamClient(context, KiottyNet_createStreamClient(context, &config, &tls))
{
}

inline KiottyNet_StreamClient::KiottyNet_StreamClient(KiottyNetContext context, KiottyNetStreamClientResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline bool KiottyNet_StreamClient::valid() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetCode KiottyNet_StreamClient::code() const
{
    return _code;
}

inline KiottyNetStreamClientHandle KiottyNet_StreamClient::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_StreamClient::connect()
{
    return KiottyNet_connectStreamClient(_handle.get());
}

inline void KiottyNet_StreamClient::disconnect()
{
    KiottyNet_disconnectStreamClient(_handle.get());
}

inline KiottyNetResult KiottyNet_StreamClient::read(void* buffer, size_t length, uint32_t timeout_ms)
{
    return KiottyNet_readStreamClient(_handle.get(), buffer, length, timeout_ms);
}

inline KiottyNetResult KiottyNet_StreamClient::write(KiottyNetSpan data, uint32_t timeout_ms)
{
    return KiottyNet_writeStreamClient(_handle.get(), data, timeout_ms);
}

inline KiottyNet_EventClient::KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfig& config)
    : KiottyNet_EventClient(context, KiottyNet_createEventClient(context, &config, nullptr))
{
}

inline KiottyNet_EventClient::KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfig& config, const KiottyNetTlsClientConfig& tls)
    : KiottyNet_EventClient(context, KiottyNet_createEventClient(context, &config, &tls))
{
}

inline KiottyNet_EventClient::KiottyNet_EventClient(KiottyNetContext context, KiottyNetEventClientResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline bool KiottyNet_EventClient::valid() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetCode KiottyNet_EventClient::code() const
{
    return _code;
}

inline KiottyNetEventClientHandle KiottyNet_EventClient::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_EventClient::run(const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context)
{
    return KiottyNet_runEventClient(_handle.get(), &handler, app_context);
}

inline void KiottyNet_EventClient::close()
{
    KiottyNet_closeEventClient(_handle.get());
}

inline KiottyNetCode KiottyNet_EventClient::await(uint64_t timeout_ms)
{
    return KiottyNet_awaitEventClient(_handle.get(), timeout_ms);
}

inline KiottyNetConnectionHandle KiottyNet_EventClient::connection() const
{
    return KiottyNet_getEventClientConnection(_handle.get());
}

inline KiottyNetCode KiottyNet_EventClient::notify(KiottyNetSpan data)
{
    return KiottyNet_notifyEventClient(_handle.get(), data);
}

inline KiottyNet_Server::KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfig& config)
    : KiottyNet_Server(context, KiottyNet_createServer(context, &config, nullptr))
{
}

inline KiottyNet_Server::KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfig& config, const KiottyNetTlsServerConfig& tls)
    : KiottyNet_Server(context, KiottyNet_createServer(context, &config, &tls))
{
}

inline KiottyNet_Server::KiottyNet_Server(KiottyNetContext context, KiottyNetServerResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline bool KiottyNet_Server::valid() const
{
    return _handle.get() != nullptr;
}

inline KiottyNetCode KiottyNet_Server::code() const
{
    return _code;
}

inline KiottyNetServerHandle KiottyNet_Server::handle() const
{
    return _handle.get();
}

inline KiottyNetCode KiottyNet_Server::run(const KiottyNetConnectionHandler& handler, KiottyNetAppContext app_context)
{
    return KiottyNet_runServer(_handle.get(), &handler, app_context);
}

inline void KiottyNet_Server::close()
{
    KiottyNet_closeServer(_handle.get());
}

inline KiottyNetCode KiottyNet_Server::await(uint64_t timeout_ms)
{
    return KiottyNet_awaitServer(_handle.get(), timeout_ms);
}

inline KiottyNetCode KiottyNet_Server::broadcast(KiottyNetSpan data)
{
    return KiottyNet_broadcastServer(_handle.get(), data);
}

#endif

#endif
