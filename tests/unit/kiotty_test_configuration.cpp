#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
#include "data/datasource/kiotty_memory_limits.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace net = kiotty::net::implementation;

namespace
{
    const uint16_t PORT = 8080;
    const size_t   DEFAULT_MAX_CLIENTS = 1024;

    std::string stringOf(net::Span<const char> text)
    {
        return std::string(text.data, text.length);
    }

    KiottyNetServerConfiguration tcpServer()
    {
        KiottyNetServerConfiguration configuration{};
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&configuration));
        return configuration;
    }

    KiottyNetClientConfiguration tcpClient()
    {
        KiottyNetClientConfiguration configuration{};
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
        return configuration;
    }

    bool sameBytes(const void* left, const void* right, size_t length)
    {
        return std::memcmp(left, right, length) == 0;
    }
}

TEST(Configuration, ServerDefaultsAreTheRequirementValues)
{
    KiottyNetServerConfiguration configuration = tcpServer();
    EXPECT_TRUE(net::isInitialized(configuration));
    size_t expected_max = DEFAULT_MAX_CLIENTS;
    EXPECT_EQ(expected_max, configuration.max_clients);
    EXPECT_EQ(4096u, configuration.receive_capacity);
    EXPECT_EQ(4096u, configuration.send_capacity);
    EXPECT_EQ(4096u, configuration.push_capacity);
    EXPECT_EQ(KIOTTY_NET_NO_DELAY_ON, configuration.no_delay);
    EXPECT_EQ(30000u, configuration.send_timeout_milliseconds);
}

TEST(Configuration, ClientDefaultsAreTheRequirementValues)
{
    KiottyNetClientConfiguration configuration = tcpClient();
    EXPECT_TRUE(net::isInitialized(configuration));
    EXPECT_EQ(5000u, configuration.connect_timeout_milliseconds);
    EXPECT_EQ(1u, configuration.max_trial);
    EXPECT_EQ(1000u, configuration.trial_interval_milliseconds);
    EXPECT_EQ(KIOTTY_NET_NO_DELAY_ON, configuration.no_delay);
    EXPECT_EQ(30000u, configuration.send_timeout_milliseconds);
}

TEST(Configuration, InitializeWithNullIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeServerConfiguration(nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeClientConfiguration(nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeTcpAddress(nullptr, "127.0.0.1", PORT));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializePipeAddress(nullptr, "/tmp/kiotty.sock"));
}

TEST(Address, TcpAddressCarriesTransportHostAndPort)
{
    KiottyNetAddress address{};
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, "127.0.0.1", PORT));
    EXPECT_EQ(KIOTTY_NET_TRANSPORT_TCP, address.transport);
    EXPECT_STREQ("127.0.0.1", address.host);
    EXPECT_EQ(PORT, address.port);
    EXPECT_TRUE(net::isAddressValid(address));
    EXPECT_EQ("127.0.0.1", stringOf(net::hostnameOf(address)));
}

TEST(Configuration, MaxClientsRejectsZeroAndAboveTheLimit)
{
    KiottyNetServerConfiguration configuration = tcpServer();
    KiottyNetServerConfiguration before = configuration;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerMaxClients(configuration, 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerMaxClients(configuration, net::CONNECTIONS_PER_SERVER_LIMIT + 1));
    EXPECT_TRUE(sameBytes(&before, &configuration, sizeof(configuration)));
}

TEST(Configuration, MaxClientsAcceptsTheLimit)
{
    KiottyNetServerConfiguration configuration = tcpServer();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureServerMaxClients(configuration, net::CONNECTIONS_PER_SERVER_LIMIT));
    EXPECT_EQ(net::CONNECTIONS_PER_SERVER_LIMIT, configuration.max_clients);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureServerMaxClients(configuration, 1));
    EXPECT_EQ(1u, configuration.max_clients);
}

TEST(Configuration, BufferCapacityRejectsZeroAndAboveTheCompiledLimit)
{
    KiottyNetServerConfiguration server = tcpServer();
    KiottyNetServerConfiguration server_before = server;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerBufferCapacity(server, 0, 1024, 1024));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerBufferCapacity(server, 1024, 0, 1024));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerBufferCapacity(server, 1024, 1024, 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerBufferCapacity(server, net::MemoryLimits::RECEIVE_CAPACITY + 1, 1024, 1024));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerBufferCapacity(server, 1024, net::MemoryLimits::SEND_CAPACITY + 1, 1024));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerBufferCapacity(server, 1024, 1024, net::MemoryLimits::PUSH_CAPACITY + 1));
    EXPECT_TRUE(sameBytes(&server_before, &server, sizeof(server)));

    KiottyNetClientConfiguration client = tcpClient();
    KiottyNetClientConfiguration client_before = client;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureClientBufferCapacity(client, 0, 1024, 1024));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureClientBufferCapacity(client, net::MemoryLimits::RECEIVE_CAPACITY + 1, 1024, 1024));
    EXPECT_TRUE(sameBytes(&client_before, &client, sizeof(client)));
}

TEST(Configuration, BufferCapacityAcceptsTheCompiledLimit)
{
    KiottyNetServerConfiguration configuration = tcpServer();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureServerBufferCapacity(configuration, net::MemoryLimits::RECEIVE_CAPACITY, net::MemoryLimits::SEND_CAPACITY, net::MemoryLimits::PUSH_CAPACITY));
    EXPECT_EQ(net::MemoryLimits::RECEIVE_CAPACITY, configuration.receive_capacity);
    EXPECT_EQ(net::MemoryLimits::SEND_CAPACITY, configuration.send_capacity);
    EXPECT_EQ(net::MemoryLimits::PUSH_CAPACITY, configuration.push_capacity);
}

TEST(Configuration, ConfigureWithoutInitializeIsInvalidArgument)
{
    KiottyNetServerConfiguration server{};
    KiottyNetClientConfiguration client{};
    EXPECT_FALSE(net::isInitialized(server));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerMaxClients(server, 1));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerNoDelay(server, KIOTTY_NET_NO_DELAY_OFF));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureClientRetry(client, 1, 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureClientConnectTimeout(client, 1));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerSendTimeout(server, 1));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureClientSendTimeout(client, 1));
}

TEST(Configuration, SendTimeoutAcceptsZeroAsInfiniteAndAnyOtherValue)
{
    KiottyNetServerConfiguration server = tcpServer();
    KiottyNetClientConfiguration client = tcpClient();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerSendTimeout(&server, 0));
    EXPECT_EQ(0u, server.send_timeout_milliseconds);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientSendTimeout(&client, 60000));
    EXPECT_EQ(60000u, client.send_timeout_milliseconds);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureServerSendTimeout(nullptr, 1));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureClientSendTimeout(nullptr, 1));
}

TEST(Configuration, BuildersSetTheSendTimeout)
{
    KiottyNetServerConfiguration server{};
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_ServerConfigurationBuilder(server).sendTimeout(1500).build());
    EXPECT_EQ(1500u, server.send_timeout_milliseconds);
    KiottyNetClientConfiguration client{};
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_ClientConfigurationBuilder(client).sendTimeout(0).build());
    EXPECT_EQ(0u, client.send_timeout_milliseconds);
}

TEST(Configuration, UnknownNoDelayIsInvalidArgument)
{
    KiottyNetServerConfiguration server = tcpServer();
    const KiottyNetNoDelay unknown = static_cast<KiottyNetNoDelay>(2);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureServerNoDelay(server, unknown));
    EXPECT_EQ(KIOTTY_NET_NO_DELAY_ON, server.no_delay);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureServerNoDelay(server, KIOTTY_NET_NO_DELAY_OFF));
    EXPECT_EQ(KIOTTY_NET_NO_DELAY_OFF, server.no_delay);
}

TEST(Address, InitializeRejectsMissingHostZeroPortAndTooLongHost)
{
    KiottyNetAddress address{};
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeTcpAddress(&address, nullptr, PORT));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeTcpAddress(&address, "", PORT));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeTcpAddress(&address, "127.0.0.1", 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializePipeAddress(&address, ""));
    std::string longest(KIOTTY_NET_MAX_HOST_LENGTH - 1, 'a');
    std::string too_long(KIOTTY_NET_MAX_HOST_LENGTH, 'a');
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeTcpAddress(&address, too_long.c_str(), PORT));
    EXPECT_FALSE(net::isAddressValid(address));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, longest.c_str(), PORT));
    EXPECT_EQ(longest, std::string(address.host));
}

TEST(Address, InitializeRejectsHostAndPipeNameThatAreNotUtf8WithInvalidEncoding)
{
    KiottyNetAddress address{};
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_initializeTcpAddress(&address, "host\xFF", PORT));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_initializePipeAddress(&address, "/tmp/\xC0\xAF"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_initializeTcpAddress(&address, "\xED\xA0\x80", PORT));
    EXPECT_FALSE(net::isAddressValid(address));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializePipeAddress(&address, "/tmp/\xED\x95\x9C.sock"));
}

TEST(Address, PipeAddressHasNoHostnameForVerification)
{
    KiottyNetAddress address{};
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializePipeAddress(&address, "/tmp/kiotty.sock"));
    EXPECT_EQ(KIOTTY_NET_TRANSPORT_PIPE, address.transport);
    EXPECT_TRUE(net::isAddressValid(address));
    EXPECT_TRUE(net::hostnameOf(address).empty());
}

TEST(Address, ValidityRejectsUnknownTransportZeroTcpPortEmptyAndUnterminatedHost)
{
    KiottyNetAddress address{};
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, "127.0.0.1", PORT));
    KiottyNetAddress unknown = address;
    unknown.transport = static_cast<KiottyNetTransport>(7);
    EXPECT_FALSE(net::isAddressValid(unknown));
    KiottyNetAddress zero_port = address;
    zero_port.port = 0;
    EXPECT_FALSE(net::isAddressValid(zero_port));
    KiottyNetAddress empty = address;
    empty.host[0] = '\0';
    EXPECT_FALSE(net::isAddressValid(empty));
    KiottyNetAddress unterminated = address;
    std::memset(unterminated.host, 'a', sizeof(unterminated.host));
    EXPECT_FALSE(net::isAddressValid(unterminated));
    KiottyNetAddress pipe{};
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializePipeAddress(&pipe, "/tmp/kiotty.sock"));
    EXPECT_EQ(0u, pipe.port);
    EXPECT_TRUE(net::isAddressValid(pipe));
}

TEST(Configuration, RetryRejectsZeroTrials)
{
    KiottyNetClientConfiguration configuration = tcpClient();
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureClientRetry(configuration, 0, 10));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureClientRetry(configuration, 3, 10));
    EXPECT_EQ(3u, configuration.max_trial);
    EXPECT_EQ(10u, configuration.trial_interval_milliseconds);
}

TEST(TlsConfiguration, ServerDefaultsAndRejectedValues)
{
    KiottyNetTlsServerConfiguration configuration{};
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_initializeTlsServerConfiguration(&configuration, nullptr, "key.pem", nullptr));
    EXPECT_FALSE(net::isInitialized(configuration));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&configuration, "cert.pem", "key.pem", nullptr));
    EXPECT_EQ(static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V12 | KIOTTY_NET_TLS_VERSION_V13), configuration.versions);
    EXPECT_EQ(0xFu, configuration.v12_cipher_suites);
    EXPECT_EQ(0x1Fu, configuration.v13_cipher_suites);
    EXPECT_EQ(KIOTTY_NET_CLIENT_AUTHENTICATION_NONE, configuration.client_authentication);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureTlsServerVersions(configuration, 0u));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureTlsServerVersions(configuration, 0x4u));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureTlsServerV12CipherSuites(configuration, 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureTlsServerV13CipherSuites(configuration, 0x20));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureTlsServerClientAuthentication(&configuration, KIOTTY_NET_CLIENT_AUTHENTICATION_NONE, "ca.pem"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureTlsServerClientAuthentication(&configuration, KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsServerClientAuthentication(&configuration, KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED, "ca.pem"));
    EXPECT_STREQ("ca.pem", configuration.trust_store_path);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureTlsServerVersions(configuration, static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V13)));
    EXPECT_EQ(static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V13), configuration.versions);
}

TEST(TlsConfiguration, PathsAndPasswordThatAreNotUtf8AreInvalidEncoding)
{
    KiottyNetTlsServerConfiguration server{};
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_initializeTlsServerConfiguration(&server, "cert\xFF.pem", "key.pem", nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_initializeTlsServerConfiguration(&server, "cert.pem", "key.pem", "pass\xE9"));
    EXPECT_FALSE(net::isInitialized(server));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&server, "cert.pem", "key.pem", nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_configureTlsServerClientAuthentication(&server, KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED, "ca\xFF.pem"));
    KiottyNetTlsClientConfiguration client{};
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsClientConfiguration(&client));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_configureTlsClientTrustStore(&client, "\xC3\x28"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_configureTlsClientIdentity(&client, "cert.pem", "key\x80.pem", nullptr));
    EXPECT_STREQ("", client.trust_store_path);
    EXPECT_STREQ("", client.identity.certificate_file);
}

TEST(TlsConfiguration, NoClientAuthenticationAcceptsNullOrEmptyTrustStorePath)
{
    KiottyNetTlsServerConfiguration configuration{};
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&configuration, "cert.pem", "key.pem", nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsServerClientAuthentication(&configuration, KIOTTY_NET_CLIENT_AUTHENTICATION_NONE, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsServerClientAuthentication(&configuration, KIOTTY_NET_CLIENT_AUTHENTICATION_NONE, ""));
    EXPECT_EQ(KIOTTY_NET_CLIENT_AUTHENTICATION_NONE, configuration.client_authentication);
    EXPECT_STREQ("", configuration.trust_store_path);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureTlsServerClientAuthentication(&configuration, KIOTTY_NET_CLIENT_AUTHENTICATION_OPTIONAL, ""));
}

TEST(TlsConfiguration, ClientDefaultsVerifyServerAndHostnameAndUnknownVerificationIsRejected)
{
    KiottyNetTlsClientConfiguration configuration{};
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTlsClientConfiguration(configuration));
    EXPECT_EQ(KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME, configuration.verification);
    EXPECT_STREQ("", configuration.trust_store_path);
    const KiottyNetTlsVerification unknown = static_cast<KiottyNetTlsVerification>(3);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::configureTlsClientVerification(configuration, unknown));
    EXPECT_EQ(KIOTTY_NET_TLS_VERIFICATION_SERVER_AND_HOSTNAME, configuration.verification);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureTlsClientVerification(configuration, KIOTTY_NET_TLS_VERIFICATION_SERVER));
    EXPECT_EQ(KIOTTY_NET_TLS_VERIFICATION_SERVER, configuration.verification);
    std::string too_long_password(KIOTTY_NET_MAX_PASSWORD_LENGTH, 'p');
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureTlsClientIdentity(&configuration, "cert.pem", "key.pem", too_long_password.c_str()));
    EXPECT_STREQ("", configuration.identity.certificate_file);
}
