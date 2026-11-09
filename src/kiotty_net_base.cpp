#include "kiotty_net_base.h"

#include "context/kiotty_action.h"
#include "context/kiotty_configuration.h"
#include "context/kiotty_context.h"
#include "context/kiotty_tls_configuration.h"
#include "data/repository/connection/kiotty_connection.h"

namespace net = kiotty::net::implementation;

namespace
{
    constexpr KiottyNetCode INVALID = KIOTTY_NET_CODE_INVALID_ARGUMENT;

    KiottyNetResult invalidResult()
    {
        KiottyNetResult result = { 0, INVALID };
        return result;
    }

    net::NoDelay noDelayOf(bool no_delay)
    {
        return no_delay ? net::NoDelay::ENABLED : net::NoDelay::DISABLED;
    }

    bool isUsable(const void* buffer, size_t length)
    {
        return buffer != nullptr || length == 0;
    }

    size_t defaultMaxClients()
    {
        KiottyNetServerConfiguration configuration{};
        (void)net::initializeTcpServerConfiguration(configuration, "127.0.0.1", 1);
        return configuration.max_clients;
    }
}

KiottyNetCode KiottyNet_initializeTlsServerConfiguration(KiottyNetTlsServerConfiguration* config,
                                                         const char* cert_file,
                                                         const char* key_file,
                                                         const char* key_password)
{
    if (config == nullptr) { return INVALID; }
    return net::initializeTlsServerConfiguration(*config, cert_file, key_file, key_password);
}

KiottyNetCode KiottyNet_configureTlsServerVersions(KiottyNetTlsServerConfiguration* config,
                                                   bool tls_1_2,
                                                   bool tls_1_3)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsServerVersions(*config, net::TlsVersions{ tls_1_2, tls_1_3 });
}

KiottyNetCode KiottyNet_configureTlsServerV12CipherSuites(KiottyNetTlsServerConfiguration* config,
                                                          uint32_t suites)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsServerV12CipherSuites(*config, suites);
}

KiottyNetCode KiottyNet_configureTlsServerV13CipherSuites(KiottyNetTlsServerConfiguration* config,
                                                          uint32_t suites)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsServerV13CipherSuites(*config, suites);
}

KiottyNetCode KiottyNet_configureTlsServerClientAuthentication(
    KiottyNetTlsServerConfiguration* config,
    KiottyNetClientAuthentication mode,
    const char* trust_store_path)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsServerClientAuthentication(*config, mode, trust_store_path);
}

KiottyNetCode KiottyNet_initializeTlsClientConfiguration(KiottyNetTlsClientConfiguration* config)
{
    if (config == nullptr) { return INVALID; }
    return net::initializeTlsClientConfiguration(*config);
}

KiottyNetCode KiottyNet_configureTlsClientVersions(KiottyNetTlsClientConfiguration* config,
                                                   bool tls_1_2,
                                                   bool tls_1_3)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsClientVersions(*config, net::TlsVersions{ tls_1_2, tls_1_3 });
}

KiottyNetCode KiottyNet_configureTlsClientV12CipherSuites(KiottyNetTlsClientConfiguration* config,
                                                          uint32_t suites)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsClientV12CipherSuites(*config, suites);
}

KiottyNetCode KiottyNet_configureTlsClientV13CipherSuites(KiottyNetTlsClientConfiguration* config,
                                                          uint32_t suites)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsClientV13CipherSuites(*config, suites);
}

KiottyNetCode KiottyNet_configureTlsClientTrustStore(KiottyNetTlsClientConfiguration* config,
                                                     const char* trust_store_path)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsClientTrustStore(*config, trust_store_path);
}

KiottyNetCode KiottyNet_configureTlsClientVerification(KiottyNetTlsClientConfiguration* config,
                                                       bool verify_server,
                                                       bool verify_hostname)
{
    if (config == nullptr) { return INVALID; }
    const net::TlsVerification verification{ verify_server, verify_hostname };
    return net::configureTlsClientVerification(*config, verification);
}

KiottyNetCode KiottyNet_configureTlsClientIdentity(KiottyNetTlsClientConfiguration* config,
                                                   const char* cert_file,
                                                   const char* key_file,
                                                   const char* key_password)
{
    if (config == nullptr) { return INVALID; }
    return net::configureTlsClientIdentity(*config, cert_file, key_file, key_password);
}

KiottyNetCode KiottyNet_initializeTcpServerConfiguration(KiottyNetServerConfiguration* config,
                                                         const char* host,
                                                         uint16_t port)
{
    if (config == nullptr) { return INVALID; }
    return net::initializeTcpServerConfiguration(*config, host, port);
}

KiottyNetCode KiottyNet_initializePipeServerConfiguration(KiottyNetServerConfiguration* config,
                                                          const char* pipe_name)
{
    if (config == nullptr) { return INVALID; }
    return net::initializePipeServerConfiguration(*config, pipe_name);
}

KiottyNetCode KiottyNet_configureServerMaxClients(KiottyNetServerConfiguration* config,
                                                  size_t max_clients)
{
    if (config == nullptr) { return INVALID; }
    return net::configureServerMaxClients(*config, max_clients);
}

KiottyNetCode KiottyNet_configureServerBufferCapacity(KiottyNetServerConfiguration* config,
                                                      size_t receive_capacity,
                                                      size_t send_capacity,
                                                      size_t push_capacity)
{
    if (config == nullptr) { return INVALID; }
    return net::configureServerBufferCapacity(*config, receive_capacity, send_capacity,
                                              push_capacity);
}

KiottyNetCode KiottyNet_configureServerNoDelay(KiottyNetServerConfiguration* config,
                                               bool no_delay)
{
    if (config == nullptr) { return INVALID; }
    return net::configureServerNoDelay(*config, noDelayOf(no_delay));
}

KiottyNetCode KiottyNet_initializeTcpClientConfiguration(KiottyNetClientConfiguration* config,
                                                         const char* host,
                                                         uint16_t port)
{
    if (config == nullptr) { return INVALID; }
    return net::initializeTcpClientConfiguration(*config, host, port);
}

KiottyNetCode KiottyNet_initializePipeClientConfiguration(KiottyNetClientConfiguration* config,
                                                          const char* pipe_name)
{
    if (config == nullptr) { return INVALID; }
    return net::initializePipeClientConfiguration(*config, pipe_name);
}

KiottyNetCode KiottyNet_configureClientBufferCapacity(KiottyNetClientConfiguration* config,
                                                      size_t receive_capacity,
                                                      size_t send_capacity,
                                                      size_t push_capacity)
{
    if (config == nullptr) { return INVALID; }
    return net::configureClientBufferCapacity(*config, receive_capacity, send_capacity,
                                              push_capacity);
}

KiottyNetCode KiottyNet_configureClientConnectTimeout(KiottyNetClientConfiguration* config,
                                                      uint32_t timeout_milliseconds)
{
    if (config == nullptr) { return INVALID; }
    return net::configureClientConnectTimeout(*config, timeout_milliseconds);
}

KiottyNetCode KiottyNet_configureClientRetry(KiottyNetClientConfiguration* config,
                                             uint32_t max_trial,
                                             uint32_t trial_interval_milliseconds)
{
    if (config == nullptr) { return INVALID; }
    return net::configureClientRetry(*config, max_trial, trial_interval_milliseconds);
}

KiottyNetCode KiottyNet_configureClientNoDelay(KiottyNetClientConfiguration* config,
                                               bool no_delay)
{
    if (config == nullptr) { return INVALID; }
    return net::configureClientNoDelay(*config, noDelayOf(no_delay));
}

KiottyNetConnectionAction KiottyNet_thenHold(uint32_t timeout_milliseconds)
{
    return net::thenHold(timeout_milliseconds);
}

KiottyNetConnectionAction KiottyNet_thenRead(size_t exact, uint32_t timeout_milliseconds)
{
    return net::thenRead(exact, timeout_milliseconds);
}

KiottyNetConnectionAction KiottyNet_thenReadUntil(KiottyNetSpan delimiter,
                                                  size_t max,
                                                  uint32_t timeout_milliseconds)
{
    return net::thenReadUntil(delimiter, max, timeout_milliseconds);
}

KiottyNetConnectionAction KiottyNet_thenWrite(KiottyNetSpan data, uint32_t timeout_milliseconds)
{
    return net::thenWrite(data, timeout_milliseconds);
}

KiottyNetConnectionAction KiottyNet_thenWork(void)
{
    return net::thenWork();
}

KiottyNetConnectionAction KiottyNet_thenClose(KiottyNetCode reason)
{
    return net::thenClose(reason);
}

KiottyNetConnectionDecision KiottyNet_acceptConnection(
    KiottyNetConnectionContext connection_context)
{
    return net::acceptConnection(connection_context);
}

KiottyNetConnectionDecision KiottyNet_ignoreConnection(void)
{
    return net::ignoreConnection();
}

KiottyNetConnectionInfo KiottyNet_getConnectionInfo(KiottyNetConnectionHandle connection)
{
    if (connection.owner == nullptr) { return KiottyNetConnectionInfo{}; }
    return connection.owner->info(connection.id);
}

KiottyNetCode KiottyNet_notifyConnection(KiottyNetConnectionHandle connection, KiottyNetSpan data)
{
    if (connection.owner == nullptr) { return INVALID; }
    return connection.owner->notify(connection.id, data);
}

KiottyNetResult KiottyNet_readConnectionStream(KiottyNetConnectionStream stream,
                                               void* buffer,
                                               size_t length,
                                               uint32_t timeout_milliseconds)
{
    if (stream.owner == nullptr || !isUsable(buffer, length)) { return invalidResult(); }
    const net::Span<char> target{ static_cast<char*>(buffer), length };
    return stream.owner->readStream(stream.id, target, timeout_milliseconds);
}

KiottyNetResult KiottyNet_writeConnectionStream(KiottyNetConnectionStream stream,
                                                KiottyNetSpan data,
                                                uint32_t timeout_milliseconds)
{
    if (stream.owner == nullptr) { return invalidResult(); }
    return stream.owner->writeStream(stream.id, data, timeout_milliseconds);
}

KiottyNetCode KiottyNet_writeConnectionStreamFile(KiottyNetConnectionStream stream,
                                                  const char* path,
                                                  uint64_t offset,
                                                  uint64_t length,
                                                  uint32_t timeout_milliseconds)
{
    if (stream.owner == nullptr) { return INVALID; }
    const net::FileRange range{ offset, length };
    return stream.owner->writeStreamFile(stream.id, path, range, timeout_milliseconds);
}

bool KiottyNet_isConnectionStreamOpen(KiottyNetConnectionStream stream)
{
    if (stream.owner == nullptr) { return false; }
    return stream.owner->isStreamOpen(stream.id);
}

KiottyNetCode KiottyNet_closeConnectionStream(KiottyNetConnectionStream stream,
                                              KiottyNetConnectionAction then)
{
    if (stream.owner == nullptr) { return INVALID; }
    return stream.owner->closeStream(stream.id, then);
}

KiottyNetContextResult KiottyNet_initializeContext(void)
{
    const net::ContextResult opened = net::Context::open();
    KiottyNetContextResult result = { opened.code, nullptr };
    if (opened) { result.handle = &opened.value(); }
    return result;
}

void KiottyNet_releaseContext(KiottyNetContext context)
{
    if (context == nullptr) { return; }
    context->release();
}

size_t KiottyNet_serverMemorySize(void)
{
    return sizeof(KiottyNetServer) + defaultMaxClients() * sizeof(net::Connection);
}

size_t KiottyNet_clientMemorySize(void)
{
    const size_t event = sizeof(KiottyNetEventClient);
    const size_t stream = sizeof(KiottyNetStreamClient);
    return event > stream ? event : stream;
}

KiottyNetStreamClientResult KiottyNet_createStreamClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* config,
    const KiottyNetTlsClientConfiguration* tls)
{
    KiottyNetStreamClientResult result = { INVALID, nullptr };
    if (context == nullptr || config == nullptr) { return result; }
    const net::StreamClientResult created = context->createStreamClient(*config, tls);
    result.code = created.code;
    if (created) { result.handle = &created.value(); }
    return result;
}

void KiottyNet_destroyStreamClient(KiottyNetContext context, KiottyNetStreamClientHandle client)
{
    if (context == nullptr || client == nullptr) { return; }
    context->destroy(*client);
}

KiottyNetCode KiottyNet_connectStreamClient(KiottyNetStreamClientHandle client)
{
    if (client == nullptr) { return INVALID; }
    return client->connect();
}

void KiottyNet_disconnectStreamClient(KiottyNetStreamClientHandle client)
{
    if (client == nullptr) { return; }
    client->disconnect();
}

KiottyNetResult KiottyNet_readStreamClient(KiottyNetStreamClientHandle client,
                                           void* buffer,
                                           size_t length,
                                           uint32_t timeout_milliseconds)
{
    if (client == nullptr || !isUsable(buffer, length)) { return invalidResult(); }
    const net::Span<char> target{ static_cast<char*>(buffer), length };
    return client->read(target, timeout_milliseconds);
}

KiottyNetResult KiottyNet_writeStreamClient(KiottyNetStreamClientHandle client,
                                            KiottyNetSpan data,
                                            uint32_t timeout_milliseconds)
{
    if (client == nullptr) { return invalidResult(); }
    return client->write(data, timeout_milliseconds);
}

KiottyNetEventClientResult KiottyNet_createEventClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* config,
    const KiottyNetTlsClientConfiguration* tls)
{
    KiottyNetEventClientResult result = { INVALID, nullptr };
    if (context == nullptr || config == nullptr) { return result; }
    const net::EventClientResult created = context->createEventClient(*config, tls);
    result.code = created.code;
    if (created) { result.handle = &created.value(); }
    return result;
}

void KiottyNet_destroyEventClient(KiottyNetContext context, KiottyNetEventClientHandle client)
{
    if (context == nullptr || client == nullptr) { return; }
    context->destroy(*client);
}

KiottyNetCode KiottyNet_runEventClient(KiottyNetEventClientHandle client,
                                       const KiottyNetConnectionHandler* handler,
                                       KiottyNetAppContext app_context)
{
    if (client == nullptr || handler == nullptr) { return INVALID; }
    return client->run(*handler, app_context);
}

void KiottyNet_closeEventClient(KiottyNetEventClientHandle client)
{
    if (client == nullptr) { return; }
    client->close();
}

KiottyNetCode KiottyNet_awaitEventClient(KiottyNetEventClientHandle client,
                                         uint64_t timeout_milliseconds)
{
    if (client == nullptr) { return INVALID; }
    return client->await(timeout_milliseconds);
}

KiottyNetConnectionHandle KiottyNet_getEventClientConnection(KiottyNetEventClientHandle client)
{
    if (client == nullptr) { return KiottyNetConnectionHandle{ nullptr, 0 }; }
    return client->connection();
}

KiottyNetCode KiottyNet_notifyEventClient(KiottyNetEventClientHandle client, KiottyNetSpan data)
{
    if (client == nullptr) { return INVALID; }
    return client->notify(client->connection().id, data);
}

KiottyNetServerResult KiottyNet_createServer(KiottyNetContext context,
                                             const KiottyNetServerConfiguration* config,
                                             const KiottyNetTlsServerConfiguration* tls)
{
    KiottyNetServerResult result = { INVALID, nullptr };
    if (context == nullptr || config == nullptr) { return result; }
    const net::ServerResult created = context->createServer(*config, tls);
    result.code = created.code;
    if (created) { result.handle = &created.value(); }
    return result;
}

void KiottyNet_destroyServer(KiottyNetContext context, KiottyNetServerHandle server)
{
    if (context == nullptr || server == nullptr) { return; }
    context->destroy(*server);
}

KiottyNetCode KiottyNet_runServer(KiottyNetServerHandle server,
                                  const KiottyNetConnectionHandler* handler,
                                  KiottyNetAppContext app_context)
{
    if (server == nullptr || handler == nullptr) { return INVALID; }
    return server->run(*handler, app_context);
}

void KiottyNet_closeServer(KiottyNetServerHandle server)
{
    if (server == nullptr) { return; }
    server->close();
}

KiottyNetCode KiottyNet_awaitServer(KiottyNetServerHandle server, uint64_t timeout_milliseconds)
{
    if (server == nullptr) { return INVALID; }
    return server->await(timeout_milliseconds);
}

KiottyNetCode KiottyNet_broadcastServer(KiottyNetServerHandle server, KiottyNetSpan data)
{
    if (server == nullptr) { return INVALID; }
    return server->broadcast(data);
}
