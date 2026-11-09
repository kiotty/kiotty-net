#include "kiotty_net_base.h"

#include "context/kiotty_action.h"
#include "context/kiotty_configuration.h"
#include "context/kiotty_context.h"
#include "context/kiotty_tls_configuration.h"
#include "data/repository/connection/kiotty_callback_handler.h"
#include "core/kiotty_text.h"
#include "data/repository/connection/kiotty_connection.h"

namespace net = kiotty::net::implementation;

namespace
{
    constexpr KiottyNetCode INVALID = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    constexpr size_t        MAX_HOST_TEXT = KIOTTY_NET_MAX_HOST_LENGTH - 1;
    constexpr size_t        MAX_PATH_TEXT = KIOTTY_NET_MAX_PATH_LENGTH - 1;
    constexpr size_t        MAX_PASSWORD_TEXT = KIOTTY_NET_MAX_PASSWORD_LENGTH - 1;

    using IdentityResult = net::Result<net::TlsIdentityText, KiottyNetCode>;

    net::TextResult parseOptionalText(const char* text, size_t max_length)
    {
        if (text == nullptr) { return net::TextResult::success(net::Span<const char>{ text, 0 }); }
        return net::parseText(text, max_length);
    }

    IdentityResult parseIdentity(const char* certificate_file,
                                 const char* key_file,
                                 const char* key_password)
    {
        const net::TextResult certificate = net::parseText(certificate_file, MAX_PATH_TEXT);
        const net::TextResult key = net::parseText(key_file, MAX_PATH_TEXT);
        const net::TextResult password = parseOptionalText(key_password, MAX_PASSWORD_TEXT);
        if (!certificate) { return IdentityResult::failure(certificate.code); }
        if (!key) { return IdentityResult::failure(key.code); }
        if (!password) { return IdentityResult::failure(password.code); }
        const net::TlsIdentityText identity{ certificate.value, key.value, password.value };
        return IdentityResult::success(identity);
    }

    KiottyNetResult invalidResult()
    {
        KiottyNetResult result = { 0, INVALID };
        return result;
    }

    template <typename Configuration>
    size_t stagesOf(const Configuration& configuration)
    {
        const size_t receive = configuration.receive_capacity;
        const size_t send = configuration.send_capacity;
        const size_t push = configuration.push_capacity;
        return receive + send + push;
    }

    bool isUsable(const void* buffer, size_t length)
    {
        return buffer != nullptr || length == 0;
    }
}

KiottyNetCode KiottyNet_initializeTlsServerConfiguration(
    KiottyNetTlsServerConfiguration* configuration,
    const char* certificate_file,
    const char* key_file,
    const char* key_password)
{
    if (configuration == nullptr) { return INVALID; }
    const IdentityResult identity = parseIdentity(certificate_file, key_file, key_password);
    if (!identity) { return identity.code; }
    return net::initializeTlsServerConfiguration(*configuration, identity.value);
}

KiottyNetCode KiottyNet_configureTlsServerVersions(KiottyNetTlsServerConfiguration* configuration,
                                                   uint32_t versions)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsServerVersions(*configuration, versions);
}

KiottyNetCode KiottyNet_configureTlsServerV12CipherSuites(
    KiottyNetTlsServerConfiguration* configuration,
    uint32_t suites)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsServerV12CipherSuites(*configuration, suites);
}

KiottyNetCode KiottyNet_configureTlsServerV13CipherSuites(
    KiottyNetTlsServerConfiguration* configuration,
    uint32_t suites)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsServerV13CipherSuites(*configuration, suites);
}

KiottyNetCode KiottyNet_configureTlsServerClientAuthentication(
    KiottyNetTlsServerConfiguration* configuration,
    KiottyNetClientAuthentication mode,
    const char* trust_store_path)
{
    if (configuration == nullptr) { return INVALID; }
    const net::TextResult path = parseOptionalText(trust_store_path, MAX_PATH_TEXT);
    if (!path) { return path.code; }
    return net::configureTlsServerClientAuthentication(*configuration, mode, path.value);
}

KiottyNetCode KiottyNet_initializeTlsClientConfiguration(
    KiottyNetTlsClientConfiguration* configuration)
{
    if (configuration == nullptr) { return INVALID; }
    return net::initializeTlsClientConfiguration(*configuration);
}

KiottyNetCode KiottyNet_configureTlsClientVersions(KiottyNetTlsClientConfiguration* configuration,
                                                   uint32_t versions)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsClientVersions(*configuration, versions);
}

KiottyNetCode KiottyNet_configureTlsClientV12CipherSuites(
    KiottyNetTlsClientConfiguration* configuration,
    uint32_t suites)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsClientV12CipherSuites(*configuration, suites);
}

KiottyNetCode KiottyNet_configureTlsClientV13CipherSuites(
    KiottyNetTlsClientConfiguration* configuration,
    uint32_t suites)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsClientV13CipherSuites(*configuration, suites);
}

KiottyNetCode KiottyNet_configureTlsClientTrustStore(KiottyNetTlsClientConfiguration* configuration,
                                                     const char* trust_store_path)
{
    if (configuration == nullptr) { return INVALID; }
    const net::TextResult path = net::parseText(trust_store_path, MAX_PATH_TEXT);
    if (!path) { return path.code; }
    return net::configureTlsClientTrustStore(*configuration, path.value);
}

KiottyNetCode KiottyNet_configureTlsClientVerification(
    KiottyNetTlsClientConfiguration* configuration,
    KiottyNetTlsVerification verification)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureTlsClientVerification(*configuration, verification);
}

KiottyNetCode KiottyNet_configureTlsClientIdentity(KiottyNetTlsClientConfiguration* configuration,
                                                   const char* certificate_file,
                                                   const char* key_file,
                                                   const char* key_password)
{
    if (configuration == nullptr) { return INVALID; }
    const IdentityResult identity = parseIdentity(certificate_file, key_file, key_password);
    if (!identity) { return identity.code; }
    return net::configureTlsClientIdentity(*configuration, identity.value);
}

KiottyNetCode KiottyNet_initializeTcpServerConfiguration(
    KiottyNetServerConfiguration* configuration,
    const char* host,
    uint16_t port)
{
    if (configuration == nullptr) { return INVALID; }
    const net::TextResult text = net::parseText(host, MAX_HOST_TEXT);
    if (!text) { return text.code; }
    return net::initializeTcpServerConfiguration(*configuration, text.value, port);
}

KiottyNetCode KiottyNet_initializePipeServerConfiguration(
    KiottyNetServerConfiguration* configuration,
    const char* pipe_name)
{
    if (configuration == nullptr) { return INVALID; }
    const net::TextResult text = net::parseText(pipe_name, MAX_HOST_TEXT);
    if (!text) { return text.code; }
    return net::initializePipeServerConfiguration(*configuration, text.value);
}

KiottyNetCode KiottyNet_configureServerMaxClients(KiottyNetServerConfiguration* configuration,
                                                  size_t max_clients)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureServerMaxClients(*configuration, max_clients);
}

KiottyNetCode KiottyNet_configureServerBufferCapacity(KiottyNetServerConfiguration* configuration,
                                                      size_t receive_capacity,
                                                      size_t send_capacity,
                                                      size_t push_capacity)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureServerBufferCapacity(*configuration, receive_capacity, send_capacity,
                                              push_capacity);
}

KiottyNetCode KiottyNet_configureServerNoDelay(KiottyNetServerConfiguration* configuration,
                                               KiottyNetNoDelay no_delay)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureServerNoDelay(*configuration, no_delay);
}

KiottyNetCode KiottyNet_initializeTcpClientConfiguration(
    KiottyNetClientConfiguration* configuration,
    const char* host,
    uint16_t port)
{
    if (configuration == nullptr) { return INVALID; }
    const net::TextResult text = net::parseText(host, MAX_HOST_TEXT);
    if (!text) { return text.code; }
    return net::initializeTcpClientConfiguration(*configuration, text.value, port);
}

KiottyNetCode KiottyNet_initializePipeClientConfiguration(
    KiottyNetClientConfiguration* configuration,
    const char* pipe_name)
{
    if (configuration == nullptr) { return INVALID; }
    const net::TextResult text = net::parseText(pipe_name, MAX_HOST_TEXT);
    if (!text) { return text.code; }
    return net::initializePipeClientConfiguration(*configuration, text.value);
}

KiottyNetCode KiottyNet_configureClientBufferCapacity(KiottyNetClientConfiguration* configuration,
                                                      size_t receive_capacity,
                                                      size_t send_capacity,
                                                      size_t push_capacity)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureClientBufferCapacity(*configuration, receive_capacity, send_capacity,
                                              push_capacity);
}

KiottyNetCode KiottyNet_configureClientConnectTimeout(KiottyNetClientConfiguration* configuration,
                                                      uint32_t timeout_milliseconds)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureClientConnectTimeout(*configuration, timeout_milliseconds);
}

KiottyNetCode KiottyNet_configureClientRetry(KiottyNetClientConfiguration* configuration,
                                             uint32_t max_trial,
                                             uint32_t trial_interval_milliseconds)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureClientRetry(*configuration, max_trial, trial_interval_milliseconds);
}

KiottyNetCode KiottyNet_configureClientNoDelay(KiottyNetClientConfiguration* configuration,
                                               KiottyNetNoDelay no_delay)
{
    if (configuration == nullptr) { return INVALID; }
    return net::configureClientNoDelay(*configuration, no_delay);
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
    const net::TextResult text = net::parseText(path, MAX_PATH_TEXT);
    if (!text) { return text.code; }
    const net::FileRange range{ offset, length };
    return stream.owner->writeStreamFile(stream.id, text.value, range, timeout_milliseconds);
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

size_t KiottyNet_serverMemorySize(const KiottyNetServerConfiguration* configuration)
{
    if (configuration == nullptr || !net::isInitialized(*configuration)) { return 0; }
    const size_t stages = stagesOf(*configuration);
    const size_t connection = sizeof(net::Connection) + stages;
    return sizeof(KiottyNetServer) + configuration->max_clients * connection;
}

size_t KiottyNet_clientMemorySize(const KiottyNetClientConfiguration* configuration)
{
    if (configuration == nullptr || !net::isInitialized(*configuration)) { return 0; }
    const size_t event = sizeof(KiottyNetEventClient);
    const size_t stream = sizeof(KiottyNetStreamClient);
    const size_t largest = event > stream ? event : stream;
    return largest + stagesOf(*configuration);
}

KiottyNetStreamClientResult KiottyNet_createStreamClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* configuration,
    const KiottyNetTlsClientConfiguration* tls)
{
    KiottyNetStreamClientResult result = { INVALID, nullptr };
    if (context == nullptr || configuration == nullptr) { return result; }
    const net::StreamClientResult created = context->createStreamClient(*configuration, tls);
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
    const KiottyNetClientConfiguration* configuration,
    const KiottyNetTlsClientConfiguration* tls)
{
    KiottyNetEventClientResult result = { INVALID, nullptr };
    if (context == nullptr || configuration == nullptr) { return result; }
    const net::EventClientResult created = context->createEventClient(*configuration, tls);
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
    const net::CallbackHandler callbacks(*handler, app_context, *client);
    return client->run(callbacks);
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
                                             const KiottyNetServerConfiguration* configuration,
                                             const KiottyNetTlsServerConfiguration* tls)
{
    KiottyNetServerResult result = { INVALID, nullptr };
    if (context == nullptr || configuration == nullptr) { return result; }
    const net::ServerResult created = context->createServer(*configuration, tls);
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
    const net::CallbackHandler callbacks(*handler, app_context, *server);
    return server->run(callbacks);
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
