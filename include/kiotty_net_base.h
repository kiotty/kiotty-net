#ifndef KIOTTY_NET_BASE_H
#define KIOTTY_NET_BASE_H

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

#define KIOTTY_NET_MAX_HOST_LENGTH     256
#define KIOTTY_NET_MAX_PATH_LENGTH     260
#define KIOTTY_NET_MAX_PASSWORD_LENGTH 128

typedef int32_t KiottyNetBool;

typedef enum KiottyNetCode
{
    KIOTTY_NET_CODE_SUCCESS,
    KIOTTY_NET_CODE_PEER_CLOSED,
    KIOTTY_NET_CODE_SOCKET_ERROR,
    KIOTTY_NET_CODE_PROTOCOL_ERROR,
    KIOTTY_NET_CODE_REQUEST_TOO_LARGE,
    KIOTTY_NET_CODE_UNHANDLED_EVENT,
    KIOTTY_NET_CODE_ILLEGAL_TRANSITION,
    KIOTTY_NET_CODE_SESSION_EXHAUSTED,
    KIOTTY_NET_CODE_DUPLICATE_RULE,
    KIOTTY_NET_CODE_TIMEOUT,
    KIOTTY_NET_CODE_TABLE_FULL,
    KIOTTY_NET_CODE_BUFFER_FULL,
    KIOTTY_NET_CODE_NOT_FOUND,
    KIOTTY_NET_CODE_INVALID_ARGUMENT,
    KIOTTY_NET_CODE_CLOSED,
    KIOTTY_NET_CODE_BUSY,
    KIOTTY_NET_CODE_TLS_NOT_SUPPORTED,
    KIOTTY_NET_CODE_TLS_CERTIFICATE_NOT_FOUND,
    KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID,
    KIOTTY_NET_CODE_TLS_VERIFY_FAILED,
    KIOTTY_NET_CODE_TLS_HOSTNAME_MISMATCH,
    KIOTTY_NET_CODE_TLS_HANDSHAKE_FAILED,
    KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT,
    KIOTTY_NET_CODE_TLS_RECORD_ERROR,
    KIOTTY_NET_CODE_OUT_OF_MEMORY,
    KIOTTY_NET_CODE_INVALID_ENCODING,
    KIOTTY_NET_CODE_PEER_NOT_READING
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
    KIOTTY_NET_TLS_VERSION_V12 = 1 << 0,
    KIOTTY_NET_TLS_VERSION_V13 = 1 << 1
} KiottyNetTlsVersion;

typedef enum KiottyNetTlsV12CipherSuite
{
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_AES256_GCM_SHA384 = 1 << 0,
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_AES256_GCM_SHA384   = 1 << 1,
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_ECDSA_CHACHA20_POLY1305 = 1 << 2,
    KIOTTY_NET_TLS_V12_CIPHER_SUITE_ECDHE_RSA_CHACHA20_POLY1305   = 1 << 3
} KiottyNetTlsV12CipherSuite;

typedef enum KiottyNetTlsV13CipherSuite
{
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_GCM_SHA256       = 1 << 0,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_256_GCM_SHA384       = 1 << 1,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_CHACHA20_POLY1305_SHA256 = 1 << 2,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM_SHA256       = 1 << 3,
    KIOTTY_NET_TLS_V13_CIPHER_SUITE_AES_128_CCM8_SHA256      = 1 << 4
} KiottyNetTlsV13CipherSuite;

typedef enum KiottyNetTlsVerification
{
    KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME,
    KIOTTY_NET_TLS_VERIFICATION_SERVER,
    KIOTTY_NET_TLS_VERIFICATION_NONE
} KiottyNetTlsVerification;

typedef enum KiottyNetNoDelay
{
    KIOTTY_NET_NO_DELAY_OFF,
    KIOTTY_NET_NO_DELAY_ON
} KiottyNetNoDelay;

typedef enum KiottyNetClientAuthentication
{
    KIOTTY_NET_CLIENT_AUTHENTICATION_NONE,
    KIOTTY_NET_CLIENT_AUTHENTICATION_OPTIONAL,
    KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED
} KiottyNetClientAuthentication;

typedef struct KiottyNetTlsIdentity
{
    char certificate_file[KIOTTY_NET_MAX_PATH_LENGTH];
    char key_file[KIOTTY_NET_MAX_PATH_LENGTH];
    char key_password[KIOTTY_NET_MAX_PASSWORD_LENGTH];
} KiottyNetTlsIdentity;

typedef struct KiottyNetTlsServerConfiguration
{
    uint32_t             initialization_mark;
    uint32_t             versions;
    uint32_t             v12_cipher_suites;
    uint32_t             v13_cipher_suites;
    KiottyNetTlsIdentity identity;
    KiottyNetClientAuthentication  client_authentication;
    char                 trust_store_path[KIOTTY_NET_MAX_PATH_LENGTH];
} KiottyNetTlsServerConfiguration;

typedef struct KiottyNetTlsClientConfiguration
{
    uint32_t             initialization_mark;
    uint32_t             versions;
    uint32_t             v12_cipher_suites;
    uint32_t             v13_cipher_suites;
    KiottyNetTlsVerification verification;
    char                 trust_store_path[KIOTTY_NET_MAX_PATH_LENGTH];
    KiottyNetTlsIdentity identity;
} KiottyNetTlsClientConfiguration;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTlsServerConfiguration(
    KiottyNetTlsServerConfiguration* configuration,
    const char* certificate_file,
    const char* key_file,
    const char* key_password);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerVersions(
    KiottyNetTlsServerConfiguration* configuration,
    uint32_t versions);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerV12CipherSuites(
    KiottyNetTlsServerConfiguration* configuration,
    uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerV13CipherSuites(
    KiottyNetTlsServerConfiguration* configuration,
    uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsServerClientAuthentication(
    KiottyNetTlsServerConfiguration* configuration,
    KiottyNetClientAuthentication mode,
    const char* trust_store_path);

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTlsClientConfiguration(
    KiottyNetTlsClientConfiguration* configuration);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientVersions(
    KiottyNetTlsClientConfiguration* configuration,
    uint32_t versions);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientV12CipherSuites(
    KiottyNetTlsClientConfiguration* configuration,
    uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientV13CipherSuites(
    KiottyNetTlsClientConfiguration* configuration,
    uint32_t suites);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientTrustStore(
    KiottyNetTlsClientConfiguration* configuration,
    const char* trust_store_path);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientVerification(
    KiottyNetTlsClientConfiguration* configuration,
    KiottyNetTlsVerification verification);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureTlsClientIdentity(
    KiottyNetTlsClientConfiguration* configuration,
    const char* certificate_file,
    const char* key_file,
    const char* key_password);

typedef enum KiottyNetTransport
{
    KIOTTY_NET_TRANSPORT_TCP,
    KIOTTY_NET_TRANSPORT_PIPE
} KiottyNetTransport;

typedef struct KiottyNetAddress
{
    KiottyNetTransport transport;
    char               host[KIOTTY_NET_MAX_HOST_LENGTH];
    uint16_t           port;
} KiottyNetAddress;

typedef struct KiottyNetServerConfiguration
{
    uint32_t          initialization_mark;
    size_t            max_clients;
    size_t            receive_capacity;
    size_t            send_capacity;
    size_t            push_capacity;
    KiottyNetNoDelay  no_delay;
    uint32_t          send_timeout_milliseconds;
} KiottyNetServerConfiguration;

typedef struct KiottyNetClientConfiguration
{
    uint32_t          initialization_mark;
    size_t            receive_capacity;
    size_t            send_capacity;
    size_t            push_capacity;
    uint32_t          connect_timeout_milliseconds;
    uint32_t          max_trial;
    uint32_t          trial_interval_milliseconds;
    KiottyNetNoDelay  no_delay;
    uint32_t          send_timeout_milliseconds;
} KiottyNetClientConfiguration;

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeTcpAddress(
    KiottyNetAddress* address,
    const char* host,
    uint16_t port);
KIOTTY_NET_API KiottyNetCode KiottyNet_initializePipeAddress(
    KiottyNetAddress* address,
    const char* pipe_name);

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeServerConfiguration(
    KiottyNetServerConfiguration* configuration);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerMaxClients(
    KiottyNetServerConfiguration* configuration,
    size_t max_clients);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerBufferCapacity(
    KiottyNetServerConfiguration* configuration,
    size_t receive_capacity,
    size_t send_capacity,
    size_t push_capacity);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerNoDelay(
    KiottyNetServerConfiguration* configuration,
    KiottyNetNoDelay no_delay);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureServerSendTimeout(
    KiottyNetServerConfiguration* configuration,
    uint32_t timeout_milliseconds);

KIOTTY_NET_API KiottyNetCode KiottyNet_initializeClientConfiguration(
    KiottyNetClientConfiguration* configuration);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientBufferCapacity(
    KiottyNetClientConfiguration* configuration,
    size_t receive_capacity,
    size_t send_capacity,
    size_t push_capacity);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientConnectTimeout(
    KiottyNetClientConfiguration* configuration,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientRetry(
    KiottyNetClientConfiguration* configuration,
    uint32_t max_trial,
    uint32_t trial_interval_milliseconds);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientNoDelay(
    KiottyNetClientConfiguration* configuration,
    KiottyNetNoDelay no_delay);
KIOTTY_NET_API KiottyNetCode KiottyNet_configureClientSendTimeout(
    KiottyNetClientConfiguration* configuration,
    uint32_t timeout_milliseconds);

typedef struct KiottyNetEndpoint KiottyNetEndpoint;

typedef struct KiottyNetConnectionHandle
{
    KiottyNetEndpoint* owner;
    int64_t                   id;
} KiottyNetConnectionHandle;

typedef struct KiottyNetConnectionStream
{
    KiottyNetEndpoint* owner;
    int64_t                   id;
} KiottyNetConnectionStream;

typedef void*   KiottyNetAppContext;
typedef void*   KiottyNetConnectionContext;

typedef struct KiottyNetConnectionInfo
{
    char    hostname[KIOTTY_NET_MAX_HOST_LENGTH];
    int32_t port;
} KiottyNetConnectionInfo;

typedef enum KiottyNetActionType
{
    KIOTTY_NET_ACTION_TYPE_HOLD,
    KIOTTY_NET_ACTION_TYPE_READ,
    KIOTTY_NET_ACTION_TYPE_READ_UNTIL,
    KIOTTY_NET_ACTION_TYPE_WRITE,
    KIOTTY_NET_ACTION_TYPE_START_WORK,
    KIOTTY_NET_ACTION_TYPE_CLOSE
} KiottyNetActionType;

typedef struct KiottyNetConnectionAction
{
    KiottyNetActionType type;
    uint32_t            timeout_milliseconds;
    size_t              exact;
    KiottyNetSpan       delimiter;
    size_t              max;
    KiottyNetSpan       data;
    KiottyNetCode       reason;
} KiottyNetConnectionAction;

KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_thenHold(uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_thenRead(
    size_t exact,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_thenReadUntil(
    KiottyNetSpan delimiter,
    size_t max,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_thenWrite(
    KiottyNetSpan data,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_thenWork(void);
KIOTTY_NET_API KiottyNetConnectionAction KiottyNet_thenClose(KiottyNetCode reason);

typedef struct KiottyNetConnectionDecision
{
    KiottyNetBool              accepted;
    KiottyNetConnectionContext connection_context;
} KiottyNetConnectionDecision;

KIOTTY_NET_API KiottyNetConnectionDecision KiottyNet_acceptConnection(
    KiottyNetConnectionContext connection_context);
KIOTTY_NET_API KiottyNetConnectionDecision KiottyNet_ignoreConnection(void);

typedef struct KiottyNetConnectionHandler
{
    KiottyNetConnectionDecision (*onConnected)(
        KiottyNetAppContext app_context,
        KiottyNetConnectionInfo info);
    KiottyNetConnectionAction (*onReady)(
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle connection);
    KiottyNetConnectionAction (*onRead)(
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle connection,
        KiottyNetSpan data);
    KiottyNetConnectionAction (*onWritten)(
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle connection,
        size_t length);
    KiottyNetConnectionAction (*onTimeout)(
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle connection);
    void (*onWorkStarted)(
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionStream stream);
    void (*onDisconnected)(
        KiottyNetAppContext app_context,
        KiottyNetConnectionContext connection_context,
        KiottyNetConnectionHandle connection,
        KiottyNetCode reason);
    void (*onError)(
        KiottyNetAppContext app_context,
        KiottyNetConnectionInfo info,
        KiottyNetCode code,
        const char* message);
} KiottyNetConnectionHandler;

KIOTTY_NET_API KiottyNetConnectionInfo KiottyNet_getConnectionInfo(
    KiottyNetConnectionHandle connection);
KIOTTY_NET_API KiottyNetCode           KiottyNet_notifyConnection(
    KiottyNetConnectionHandle connection,
    KiottyNetSpan data);

KIOTTY_NET_API KiottyNetResult KiottyNet_readConnectionStream(
    KiottyNetConnectionStream stream,
    void* buffer,
    size_t length,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetResult KiottyNet_writeConnectionStream(
    KiottyNetConnectionStream stream,
    KiottyNetSpan data,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetCode   KiottyNet_writeConnectionStreamFile(
    KiottyNetConnectionStream stream,
    const char* path,
    uint64_t offset,
    uint64_t length,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetBool   KiottyNet_isConnectionStreamOpen(KiottyNetConnectionStream stream);
KIOTTY_NET_API KiottyNetCode   KiottyNet_closeConnectionStream(
    KiottyNetConnectionStream stream,
    KiottyNetConnectionAction then);

typedef struct KiottyNetContextImpl* KiottyNetContext;

typedef struct KiottyNetContextResult
{
    KiottyNetCode    code;
    KiottyNetContext handle;
} KiottyNetContextResult;

KIOTTY_NET_API KiottyNetContextResult KiottyNet_initializeContext(void);
KIOTTY_NET_API void                   KiottyNet_releaseContext(KiottyNetContext context);

KIOTTY_NET_API size_t KiottyNet_serverMemorySize(const KiottyNetServerConfiguration* configuration);
KIOTTY_NET_API size_t KiottyNet_clientMemorySize(const KiottyNetClientConfiguration* configuration);

typedef struct KiottyNetStreamClient* KiottyNetStreamClientHandle;

typedef struct KiottyNetStreamClientResult
{
    KiottyNetCode               code;
    KiottyNetStreamClientHandle handle;
} KiottyNetStreamClientResult;

KIOTTY_NET_API KiottyNetStreamClientResult KiottyNet_createStreamClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* configuration);
KIOTTY_NET_API void                        KiottyNet_destroyStreamClient(
    KiottyNetContext context,
    KiottyNetStreamClientHandle client);
KIOTTY_NET_API KiottyNetCode KiottyNet_connectStreamClient(
    KiottyNetStreamClientHandle client,
    const KiottyNetAddress* address,
    const KiottyNetTlsClientConfiguration* tls);
KIOTTY_NET_API void KiottyNet_disconnectStreamClient(
    KiottyNetStreamClientHandle client);
KIOTTY_NET_API KiottyNetResult             KiottyNet_readStreamClient(
    KiottyNetStreamClientHandle client,
    void* buffer,
    size_t length,
    uint32_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetResult             KiottyNet_writeStreamClient(
    KiottyNetStreamClientHandle client,
    KiottyNetSpan data,
    uint32_t timeout_milliseconds);

typedef struct KiottyNetEventClient* KiottyNetEventClientHandle;

typedef struct KiottyNetEventClientResult
{
    KiottyNetCode              code;
    KiottyNetEventClientHandle handle;
} KiottyNetEventClientResult;

KIOTTY_NET_API KiottyNetEventClientResult KiottyNet_createEventClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* configuration);
KIOTTY_NET_API void                       KiottyNet_destroyEventClient(
    KiottyNetContext context,
    KiottyNetEventClientHandle client);
KIOTTY_NET_API KiottyNetCode              KiottyNet_runEventClient(
    KiottyNetEventClientHandle client,
    const KiottyNetAddress* address,
    const KiottyNetTlsClientConfiguration* tls,
    const KiottyNetConnectionHandler* handler,
    KiottyNetAppContext app_context);
KIOTTY_NET_API void KiottyNet_closeEventClient(
    KiottyNetEventClientHandle client);
KIOTTY_NET_API KiottyNetCode              KiottyNet_awaitEventClient(
    KiottyNetEventClientHandle client,
    uint64_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetConnectionHandle KiottyNet_getEventClientConnection(
    KiottyNetEventClientHandle client);
KIOTTY_NET_API KiottyNetCode              KiottyNet_notifyEventClient(
    KiottyNetEventClientHandle client,
    KiottyNetSpan data);

typedef struct KiottyNetServer* KiottyNetServerHandle;

typedef struct KiottyNetServerResult
{
    KiottyNetCode         code;
    KiottyNetServerHandle handle;
} KiottyNetServerResult;

KIOTTY_NET_API KiottyNetServerResult KiottyNet_createServer(
    KiottyNetContext context,
    const KiottyNetServerConfiguration* configuration);
KIOTTY_NET_API void                  KiottyNet_destroyServer(
    KiottyNetContext context,
    KiottyNetServerHandle server);
KIOTTY_NET_API KiottyNetCode         KiottyNet_runServer(
    KiottyNetServerHandle server,
    const KiottyNetAddress* address,
    const KiottyNetTlsServerConfiguration* tls,
    const KiottyNetConnectionHandler* handler,
    KiottyNetAppContext app_context);
KIOTTY_NET_API void                  KiottyNet_closeServer(KiottyNetServerHandle server);
KIOTTY_NET_API KiottyNetCode         KiottyNet_awaitServer(
    KiottyNetServerHandle server,
    uint64_t timeout_milliseconds);
KIOTTY_NET_API KiottyNetCode         KiottyNet_broadcastServer(
    KiottyNetServerHandle server,
    KiottyNetSpan data);

#ifdef __cplusplus
}

#include <functional>

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
        inline OwnedHandle<Handle, Destroy>& OwnedHandle<Handle, Destroy>::operator=(
            OwnedHandle&& other)
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

class KiottyNet_ServerConfigurationBuilder
{
public:
    explicit KiottyNet_ServerConfigurationBuilder(KiottyNetServerConfiguration& configuration);

    KiottyNet_ServerConfigurationBuilder& maxClients(size_t max_clients);
    KiottyNet_ServerConfigurationBuilder& bufferCapacity(
        size_t receive_capacity,
        size_t send_capacity,
        size_t push_capacity);
    KiottyNet_ServerConfigurationBuilder& noDelay(KiottyNetNoDelay no_delay);
    KiottyNet_ServerConfigurationBuilder& sendTimeout(uint32_t timeout_milliseconds);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetServerConfiguration&    _configuration;
    KiottyNetCode             _code;
};

class KiottyNet_ClientConfigurationBuilder
{
public:
    explicit KiottyNet_ClientConfigurationBuilder(KiottyNetClientConfiguration& configuration);

    KiottyNet_ClientConfigurationBuilder& bufferCapacity(
        size_t receive_capacity,
        size_t send_capacity,
        size_t push_capacity);
    KiottyNet_ClientConfigurationBuilder& connectTimeout(uint32_t timeout_milliseconds);
    KiottyNet_ClientConfigurationBuilder& retry(
        uint32_t max_trial,
        uint32_t trial_interval_milliseconds);
    KiottyNet_ClientConfigurationBuilder& noDelay(KiottyNetNoDelay no_delay);
    KiottyNet_ClientConfigurationBuilder& sendTimeout(uint32_t timeout_milliseconds);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetClientConfiguration&    _configuration;
    KiottyNetCode             _code;
};

class KiottyNet_TlsServerConfigurationBuilder
{
public:
    KiottyNet_TlsServerConfigurationBuilder(
        KiottyNetTlsServerConfiguration& configuration,
        const char* certificate_file,
        const char* key_file,
        const char* key_password = nullptr);

    KiottyNet_TlsServerConfigurationBuilder& versions(uint32_t versions);
    KiottyNet_TlsServerConfigurationBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigurationBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsServerConfigurationBuilder& clientAuthentication(
        KiottyNetClientAuthentication mode,
        const char* trust_store_path);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetTlsServerConfiguration& _configuration;
    KiottyNetCode             _code;
};

class KiottyNet_TlsClientConfigurationBuilder
{
public:
    explicit KiottyNet_TlsClientConfigurationBuilder(KiottyNetTlsClientConfiguration& configuration);

    KiottyNet_TlsClientConfigurationBuilder& versions(uint32_t versions);
    KiottyNet_TlsClientConfigurationBuilder& v12CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigurationBuilder& v13CipherSuites(uint32_t suites);
    KiottyNet_TlsClientConfigurationBuilder& trustStore(const char* trust_store_path);
    KiottyNet_TlsClientConfigurationBuilder& verification(KiottyNetTlsVerification verification);
    KiottyNet_TlsClientConfigurationBuilder& identity(
        const char* certificate_file,
        const char* key_file,
        const char* key_password = nullptr);

    KiottyNetCode build() const;

private:
    void record(KiottyNetCode code);

    KiottyNetTlsClientConfiguration& _configuration;
    KiottyNetCode             _code;
};

class KiottyNet_Connection
{
public:
    explicit KiottyNet_Connection(KiottyNetConnectionHandle handle);

    KiottyNetConnectionHandle handle() const;
    KiottyNetConnectionInfo   info() const;
    KiottyNetCode             notify(KiottyNetSpan data);

    bool operator==(const KiottyNet_Connection& other) const;
    bool operator!=(const KiottyNet_Connection& other) const;
    bool operator<(const KiottyNet_Connection& other) const;

private:
    KiottyNetConnectionHandle _handle;
};

class KiottyNet_ConnectionStream
{
public:
    explicit KiottyNet_ConnectionStream(KiottyNetConnectionStream stream);
    KiottyNet_ConnectionStream(KiottyNet_ConnectionStream&& other);
    KiottyNet_ConnectionStream& operator=(KiottyNet_ConnectionStream&& other);
    ~KiottyNet_ConnectionStream();

    KiottyNet_ConnectionStream(const KiottyNet_ConnectionStream&) = delete;
    KiottyNet_ConnectionStream& operator=(const KiottyNet_ConnectionStream&) = delete;

    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_milliseconds);
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);
    KiottyNetCode   writeFile(
        const char* path,
        uint64_t offset,
        uint64_t length,
        uint32_t timeout_milliseconds);
    bool            open() const;
    KiottyNetCode   close(KiottyNetConnectionAction then);

private:
    KiottyNetConnectionStream release();
    void                      reset();

    KiottyNetConnectionStream _stream;
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

    explicit operator bool() const;
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
    KiottyNet_StreamClient(KiottyNetContext context, const KiottyNetClientConfiguration& configuration);

    explicit operator bool() const;
    KiottyNetCode               code() const;
    KiottyNetStreamClientHandle handle() const;

    KiottyNetCode   connect(const KiottyNetAddress& address);
    KiottyNetCode   connect(const KiottyNetAddress& address, const KiottyNetTlsClientConfiguration& tls);
    void            disconnect();
    KiottyNetResult read(void* buffer, size_t length, uint32_t timeout_milliseconds);
    KiottyNetResult write(KiottyNetSpan data, uint32_t timeout_milliseconds);

private:
    KiottyNet_StreamClient(KiottyNetContext context, KiottyNetStreamClientResult result);

    KiottyNetCode _code;
    kiotty::net::OwnedHandle<KiottyNetStreamClientHandle, KiottyNet_destroyStreamClient> _handle;
};

class KiottyNet_EventClient
{
public:
    KiottyNet_EventClient(KiottyNetContext context, const KiottyNetClientConfiguration& configuration);

    explicit operator bool() const;
    KiottyNetCode              code() const;
    KiottyNetEventClientHandle handle() const;

    KiottyNetCode             run(
        const KiottyNetAddress& address,
        const KiottyNetConnectionHandler& handler,
        KiottyNetAppContext app_context);
    KiottyNetCode             run(
        const KiottyNetAddress& address,
        const KiottyNetTlsClientConfiguration& tls,
        const KiottyNetConnectionHandler& handler,
        KiottyNetAppContext app_context);
    void                      close();
    KiottyNetCode             await(uint64_t timeout_milliseconds);
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
    KiottyNet_Server(KiottyNetContext context, const KiottyNetServerConfiguration& configuration);

    explicit operator bool() const;
    KiottyNetCode         code() const;
    KiottyNetServerHandle handle() const;

    KiottyNetCode run(
        const KiottyNetAddress& address,
        const KiottyNetConnectionHandler& handler,
        KiottyNetAppContext app_context);
    KiottyNetCode run(
        const KiottyNetAddress& address,
        const KiottyNetTlsServerConfiguration& tls,
        const KiottyNetConnectionHandler& handler,
        KiottyNetAppContext app_context);
    void          close();
    KiottyNetCode await(uint64_t timeout_milliseconds);
    KiottyNetCode broadcast(KiottyNetSpan data);

private:
    KiottyNet_Server(KiottyNetContext context, KiottyNetServerResult result);

    KiottyNetCode _code;
    kiotty::net::OwnedHandle<KiottyNetServerHandle, KiottyNet_destroyServer> _handle;
};

inline KiottyNet_ServerConfigurationBuilder::KiottyNet_ServerConfigurationBuilder(
    KiottyNetServerConfiguration& configuration)
    : _configuration(configuration)
    , _code(KiottyNet_initializeServerConfiguration(&configuration))
{
}

inline KiottyNet_ServerConfigurationBuilder& KiottyNet_ServerConfigurationBuilder::maxClients(
    size_t max_clients)
{
    record(KiottyNet_configureServerMaxClients(&_configuration, max_clients));
    return *this;
}

inline KiottyNet_ServerConfigurationBuilder& KiottyNet_ServerConfigurationBuilder::bufferCapacity(
    size_t receive_capacity,
    size_t send_capacity,
    size_t push_capacity)
{
    record(KiottyNet_configureServerBufferCapacity(
        &_configuration,
        receive_capacity,
        send_capacity,
        push_capacity));
    return *this;
}

inline KiottyNet_ServerConfigurationBuilder& KiottyNet_ServerConfigurationBuilder::noDelay(
    KiottyNetNoDelay no_delay)
{
    record(KiottyNet_configureServerNoDelay(&_configuration, no_delay));
    return *this;
}

inline KiottyNet_ServerConfigurationBuilder& KiottyNet_ServerConfigurationBuilder::sendTimeout(
    uint32_t timeout_milliseconds)
{
    record(KiottyNet_configureServerSendTimeout(&_configuration, timeout_milliseconds));
    return *this;
}

inline KiottyNetCode KiottyNet_ServerConfigurationBuilder::build() const
{
    return _code;
}

inline void KiottyNet_ServerConfigurationBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_NET_CODE_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_ClientConfigurationBuilder::KiottyNet_ClientConfigurationBuilder(
    KiottyNetClientConfiguration& configuration)
    : _configuration(configuration)
    , _code(KiottyNet_initializeClientConfiguration(&configuration))
{
}

inline KiottyNet_ClientConfigurationBuilder& KiottyNet_ClientConfigurationBuilder::bufferCapacity(
    size_t receive_capacity,
    size_t send_capacity,
    size_t push_capacity)
{
    record(KiottyNet_configureClientBufferCapacity(
        &_configuration,
        receive_capacity,
        send_capacity,
        push_capacity));
    return *this;
}

inline KiottyNet_ClientConfigurationBuilder& KiottyNet_ClientConfigurationBuilder::connectTimeout(
    uint32_t timeout_milliseconds)
{
    record(KiottyNet_configureClientConnectTimeout(&_configuration, timeout_milliseconds));
    return *this;
}

inline KiottyNet_ClientConfigurationBuilder& KiottyNet_ClientConfigurationBuilder::retry(
    uint32_t max_trial,
    uint32_t trial_interval_milliseconds)
{
    record(KiottyNet_configureClientRetry(&_configuration, max_trial, trial_interval_milliseconds));
    return *this;
}

inline KiottyNet_ClientConfigurationBuilder& KiottyNet_ClientConfigurationBuilder::noDelay(
    KiottyNetNoDelay no_delay)
{
    record(KiottyNet_configureClientNoDelay(&_configuration, no_delay));
    return *this;
}

inline KiottyNet_ClientConfigurationBuilder& KiottyNet_ClientConfigurationBuilder::sendTimeout(
    uint32_t timeout_milliseconds)
{
    record(KiottyNet_configureClientSendTimeout(&_configuration, timeout_milliseconds));
    return *this;
}

inline KiottyNetCode KiottyNet_ClientConfigurationBuilder::build() const
{
    return _code;
}

inline void KiottyNet_ClientConfigurationBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_NET_CODE_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_TlsServerConfigurationBuilder::KiottyNet_TlsServerConfigurationBuilder(
    KiottyNetTlsServerConfiguration& configuration,
    const char* certificate_file,
    const char* key_file,
    const char* key_password)
    : _configuration(configuration)
    , _code(KiottyNet_initializeTlsServerConfiguration(&configuration, certificate_file, key_file, key_password))
{
}

inline KiottyNet_TlsServerConfigurationBuilder& KiottyNet_TlsServerConfigurationBuilder::versions(
    uint32_t versions)
{
    record(KiottyNet_configureTlsServerVersions(&_configuration, versions));
    return *this;
}

inline KiottyNet_TlsServerConfigurationBuilder&
KiottyNet_TlsServerConfigurationBuilder::v12CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsServerV12CipherSuites(&_configuration, suites));
    return *this;
}

inline KiottyNet_TlsServerConfigurationBuilder&
KiottyNet_TlsServerConfigurationBuilder::v13CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsServerV13CipherSuites(&_configuration, suites));
    return *this;
}

inline KiottyNet_TlsServerConfigurationBuilder&
KiottyNet_TlsServerConfigurationBuilder::clientAuthentication(
    KiottyNetClientAuthentication mode,
    const char* trust_store_path)
{
    record(
        KiottyNet_configureTlsServerClientAuthentication(&_configuration, mode, trust_store_path));
    return *this;
}

inline KiottyNetCode KiottyNet_TlsServerConfigurationBuilder::build() const
{
    return _code;
}

inline void KiottyNet_TlsServerConfigurationBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_NET_CODE_SUCCESS)
    {
        _code = code;
    }
}

inline KiottyNet_TlsClientConfigurationBuilder::KiottyNet_TlsClientConfigurationBuilder(
    KiottyNetTlsClientConfiguration& configuration)
    : _configuration(configuration)
    , _code(KiottyNet_initializeTlsClientConfiguration(&configuration))
{
}

inline KiottyNet_TlsClientConfigurationBuilder& KiottyNet_TlsClientConfigurationBuilder::versions(
    uint32_t versions)
{
    record(KiottyNet_configureTlsClientVersions(&_configuration, versions));
    return *this;
}

inline KiottyNet_TlsClientConfigurationBuilder&
KiottyNet_TlsClientConfigurationBuilder::v12CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsClientV12CipherSuites(&_configuration, suites));
    return *this;
}

inline KiottyNet_TlsClientConfigurationBuilder&
KiottyNet_TlsClientConfigurationBuilder::v13CipherSuites(uint32_t suites)
{
    record(KiottyNet_configureTlsClientV13CipherSuites(&_configuration, suites));
    return *this;
}

inline KiottyNet_TlsClientConfigurationBuilder& KiottyNet_TlsClientConfigurationBuilder::trustStore(
    const char* trust_store_path)
{
    record(KiottyNet_configureTlsClientTrustStore(&_configuration, trust_store_path));
    return *this;
}

inline KiottyNet_TlsClientConfigurationBuilder&
KiottyNet_TlsClientConfigurationBuilder::verification(KiottyNetTlsVerification verification)
{
    record(KiottyNet_configureTlsClientVerification(&_configuration, verification));
    return *this;
}

inline KiottyNet_TlsClientConfigurationBuilder& KiottyNet_TlsClientConfigurationBuilder::identity(
    const char* certificate_file,
    const char* key_file,
    const char* key_password)
{
    record(
        KiottyNet_configureTlsClientIdentity(&_configuration, certificate_file, key_file, key_password));
    return *this;
}

inline KiottyNetCode KiottyNet_TlsClientConfigurationBuilder::build() const
{
    return _code;
}

inline void KiottyNet_TlsClientConfigurationBuilder::record(KiottyNetCode code)
{
    if (_code == KIOTTY_NET_CODE_SUCCESS)
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

inline bool KiottyNet_Connection::operator==(const KiottyNet_Connection& other) const
{
    return _handle.owner == other._handle.owner && _handle.id == other._handle.id;
}

inline bool KiottyNet_Connection::operator!=(const KiottyNet_Connection& other) const
{
    return !(*this == other);
}

inline bool KiottyNet_Connection::operator<(const KiottyNet_Connection& other) const
{
    std::less<const KiottyNetEndpoint*> before;
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

inline KiottyNet_ConnectionStream::KiottyNet_ConnectionStream(KiottyNetConnectionStream stream)
    : _stream(stream)
{
}

inline KiottyNet_ConnectionStream::KiottyNet_ConnectionStream(KiottyNet_ConnectionStream&& other)
    : _stream(other.release())
{
}

inline KiottyNet_ConnectionStream& KiottyNet_ConnectionStream::operator=(
    KiottyNet_ConnectionStream&& other)
{
    if (this != &other)
    {
        reset();
        _stream = other.release();
    }
    return *this;
}

inline KiottyNet_ConnectionStream::~KiottyNet_ConnectionStream()
{
    reset();
}

inline KiottyNetResult KiottyNet_ConnectionStream::read(
    void* buffer,
    size_t length,
    uint32_t timeout_milliseconds)
{
    return KiottyNet_readConnectionStream(_stream, buffer, length, timeout_milliseconds);
}

inline KiottyNetResult KiottyNet_ConnectionStream::write(
    KiottyNetSpan data,
    uint32_t timeout_milliseconds)
{
    return KiottyNet_writeConnectionStream(_stream, data, timeout_milliseconds);
}

inline KiottyNetCode KiottyNet_ConnectionStream::writeFile(
    const char* path,
    uint64_t offset,
    uint64_t length,
    uint32_t timeout_milliseconds)
{
    return KiottyNet_writeConnectionStreamFile(_stream, path, offset, length, timeout_milliseconds);
}

inline bool KiottyNet_ConnectionStream::open() const
{
    return KiottyNet_isConnectionStreamOpen(_stream) != 0;
}

inline KiottyNetCode KiottyNet_ConnectionStream::close(KiottyNetConnectionAction then)
{
    KiottyNetCode code = KiottyNet_closeConnectionStream(_stream, then);
    if (code == KIOTTY_NET_CODE_BUFFER_FULL || code == KIOTTY_NET_CODE_INVALID_ARGUMENT)
    {
        return code;
    }
    release();
    return code;
}

inline KiottyNetConnectionStream KiottyNet_ConnectionStream::release()
{
    KiottyNetConnectionStream stream = _stream;
    _stream.owner = nullptr;
    _stream.id    = 0;
    return stream;
}

inline void KiottyNet_ConnectionStream::reset()
{
    if (_stream.owner == nullptr)
    {
        return;
    }
    KiottyNetConnectionStream stream = release();
    KiottyNetConnectionAction then = KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS);
    KiottyNet_closeConnectionStream(stream, then);
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

inline KiottyNet_Context::operator bool() const
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

inline KiottyNet_StreamClient::KiottyNet_StreamClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration& configuration)
    : KiottyNet_StreamClient(context, KiottyNet_createStreamClient(context, &configuration))
{
}

inline KiottyNet_StreamClient::KiottyNet_StreamClient(
    KiottyNetContext context,
    KiottyNetStreamClientResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline KiottyNet_StreamClient::operator bool() const
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

inline KiottyNetCode KiottyNet_StreamClient::connect(const KiottyNetAddress& address)
{
    return KiottyNet_connectStreamClient(_handle.get(), &address, nullptr);
}

inline KiottyNetCode KiottyNet_StreamClient::connect(
    const KiottyNetAddress& address,
    const KiottyNetTlsClientConfiguration& tls)
{
    return KiottyNet_connectStreamClient(_handle.get(), &address, &tls);
}

inline void KiottyNet_StreamClient::disconnect()
{
    KiottyNet_disconnectStreamClient(_handle.get());
}

inline KiottyNetResult KiottyNet_StreamClient::read(
    void* buffer,
    size_t length,
    uint32_t timeout_milliseconds)
{
    return KiottyNet_readStreamClient(_handle.get(), buffer, length, timeout_milliseconds);
}

inline KiottyNetResult KiottyNet_StreamClient::write(
    KiottyNetSpan data,
    uint32_t timeout_milliseconds)
{
    return KiottyNet_writeStreamClient(_handle.get(), data, timeout_milliseconds);
}

inline KiottyNet_EventClient::KiottyNet_EventClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration& configuration)
    : KiottyNet_EventClient(context, KiottyNet_createEventClient(context, &configuration))
{
}

inline KiottyNet_EventClient::KiottyNet_EventClient(
    KiottyNetContext context,
    KiottyNetEventClientResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline KiottyNet_EventClient::operator bool() const
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

inline KiottyNetCode KiottyNet_EventClient::run(
    const KiottyNetAddress& address,
    const KiottyNetConnectionHandler& handler,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runEventClient(_handle.get(), &address, nullptr, &handler, app_context);
}

inline KiottyNetCode KiottyNet_EventClient::run(
    const KiottyNetAddress& address,
    const KiottyNetTlsClientConfiguration& tls,
    const KiottyNetConnectionHandler& handler,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runEventClient(_handle.get(), &address, &tls, &handler, app_context);
}

inline void KiottyNet_EventClient::close()
{
    KiottyNet_closeEventClient(_handle.get());
}

inline KiottyNetCode KiottyNet_EventClient::await(uint64_t timeout_milliseconds)
{
    return KiottyNet_awaitEventClient(_handle.get(), timeout_milliseconds);
}

inline KiottyNetConnectionHandle KiottyNet_EventClient::connection() const
{
    return KiottyNet_getEventClientConnection(_handle.get());
}

inline KiottyNetCode KiottyNet_EventClient::notify(KiottyNetSpan data)
{
    return KiottyNet_notifyEventClient(_handle.get(), data);
}

inline KiottyNet_Server::KiottyNet_Server(
    KiottyNetContext context,
    const KiottyNetServerConfiguration& configuration)
    : KiottyNet_Server(context, KiottyNet_createServer(context, &configuration))
{
}

inline KiottyNet_Server::KiottyNet_Server(KiottyNetContext context, KiottyNetServerResult result)
    : _code(result.code)
    , _handle(context, result.handle)
{
}

inline KiottyNet_Server::operator bool() const
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

inline KiottyNetCode KiottyNet_Server::run(
    const KiottyNetAddress& address,
    const KiottyNetConnectionHandler& handler,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runServer(_handle.get(), &address, nullptr, &handler, app_context);
}

inline KiottyNetCode KiottyNet_Server::run(
    const KiottyNetAddress& address,
    const KiottyNetTlsServerConfiguration& tls,
    const KiottyNetConnectionHandler& handler,
    KiottyNetAppContext app_context)
{
    return KiottyNet_runServer(_handle.get(), &address, &tls, &handler, app_context);
}

inline void KiottyNet_Server::close()
{
    KiottyNet_closeServer(_handle.get());
}

inline KiottyNetCode KiottyNet_Server::await(uint64_t timeout_milliseconds)
{
    return KiottyNet_awaitServer(_handle.get(), timeout_milliseconds);
}

inline KiottyNetCode KiottyNet_Server::broadcast(KiottyNetSpan data)
{
    return KiottyNet_broadcastServer(_handle.get(), data);
}

#endif

#endif
