#include "kiotty_net_base.h"

#include "data/infra/tls/kiotty_tls_profile.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const size_t   MESSAGE_BYTES = 4;
    const uint32_t WAIT_MS = 5000;

    enum class Role : uint8_t
    {
        ECHO,
        PING
    };

    struct App
    {
        explicit App(Role app_role)
            : role(app_role)
            , ready(0)
            , read(0)
            , mutex()
            , outgoing("ping")
            , received()
        {
        }

        std::string text()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return received;
        }

        Role             role;
        std::atomic<int> ready;
        std::atomic<int> read;
        std::mutex       mutex;
        std::string      outgoing;
        std::string      received;
    };

    App& appOf(void* context)
    {
        return *static_cast<App*>(context);
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        return KiottyNet_acceptConnection(app_context);
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)connection;
        App& app = appOf(context);
        app.ready.fetch_add(1);
        if (app.role == Role::PING)
        {
            return KiottyNet_thenWrite(spanOf(app.outgoing), 0);
        }
        return KiottyNet_thenRead(MESSAGE_BYTES, 0);
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)connection;
        App& app = appOf(context);
        {
            const std::lock_guard<std::mutex> lock(app.mutex);
            app.received.append(data.data, data.length);
        }
        app.read.fetch_add(1);
        if (app.role == Role::ECHO)
        {
            return KiottyNet_thenWrite(data, 0);
        }
        return KiottyNet_thenHold(0);
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length)
    {
        (void)context;
        (void)connection;
        (void)length;
        return KiottyNet_thenRead(MESSAGE_BYTES, 0);
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)context;
        (void)connection;
        return KiottyNet_thenHold(0);
    }

    void onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        (void)context;
        (void)stream;
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetCode reason)
    {
        (void)app_context;
        (void)context;
        (void)reason;
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)app_context;
        (void)info;
        (void)code;
        (void)message;
    }

    const KiottyNetConnectionHandler HANDLER = { &onConnected, &onReady, &onRead, &onWritten, &onTimeout, &onWorkStarted, &onDisconnected, &onError };

    std::string certPath(const char* name)
    {
        return std::string(KIOTTY_TEST_CERT_DIR) + "/" + name;
    }

    KiottyNetAddress loopbackAddress(uint16_t port)
    {
        KiottyNetAddress address;
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port));
        return address;
    }

    KiottyNetTlsServerConfiguration serverTls()
    {
        KiottyNetTlsServerConfiguration tls;
        const std::string cert = certPath("server.pem");
        const std::string key = certPath("server.key");
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&tls, cert.c_str(), key.c_str(), nullptr));
        return tls;
    }

    KiottyNetTlsClientConfiguration clientTls(const char* trust_store)
    {
        KiottyNetTlsClientConfiguration tls;
        const std::string trust = certPath(trust_store);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsClientConfiguration(&tls));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsClientTrustStore(&tls, trust.c_str()));
        return tls;
    }

    bool tlsBuilt()
    {
        return kiotty::net::implementation::tlsSupported();
    }

    class RunningServer
    {
    public:
        RunningServer(KiottyNetServerHandle server, uint16_t port, const KiottyNetTlsServerConfiguration* tls, App& app)
            : _server(server)
            , _address(loopbackAddress(port))
            , _code(KIOTTY_NET_CODE_SUCCESS)
            , _thread([this, tls, &app]() { _code.store(KiottyNet_runServer(_server, &_address, tls, &HANDLER, &app)); })
        {
        }

        ~RunningServer()
        {
            stop();
        }

        RunningServer(const RunningServer&) = delete;
        RunningServer& operator=(const RunningServer&) = delete;

        KiottyNetCode stop()
        {
            KiottyNet_closeServer(_server);
            if (_thread.joinable())
            {
                _thread.join();
            }
            return _code.load();
        }

    private:
        KiottyNetServerHandle      _server;
        KiottyNetAddress           _address;
        std::atomic<KiottyNetCode> _code;
        std::thread                _thread;
    };

    class DestinationTest : public ::testing::Test
    {
    protected:
        DestinationTest()
            : context(nullptr)
        {
        }

        void SetUp() override
        {
            KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
        }

        void TearDown() override
        {
            KiottyNet_releaseContext(context);
        }

        KiottyNetServerHandle createServer()
        {
            KiottyNetServerConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&configuration));
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&configuration, 4));
            KiottyNetServerResult created = KiottyNet_createServer(context, &configuration);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        KiottyNetStreamClientHandle createStreamClient()
        {
            KiottyNetClientConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
            KiottyNetStreamClientResult created = KiottyNet_createStreamClient(context, &configuration);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        KiottyNetEventClientHandle createEventClient()
        {
            KiottyNetClientConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
            KiottyNetEventClientResult created = KiottyNet_createEventClient(context, &configuration);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        KiottyNetContext context;
    };

    std::string echoOver(KiottyNetStreamClientHandle client, const std::string& message)
    {
        KiottyNetResult written = KiottyNet_writeStreamClient(client, spanOf(message), WAIT_MS);
        if (written.code != KIOTTY_NET_CODE_SUCCESS)
        {
            return std::string();
        }
        std::string reply;
        char buffer[16] = {};
        while (reply.size() < message.size())
        {
            KiottyNetResult read = KiottyNet_readStreamClient(client, buffer, message.size() - reply.size(), WAIT_MS);
            if (read.code != KIOTTY_NET_CODE_SUCCESS)
            {
                break;
            }
            reply.append(buffer, static_cast<size_t>(read.bytes));
        }
        return reply;
    }

    std::string connectAndEcho(KiottyNetStreamClientHandle client, uint16_t port, const KiottyNetTlsClientConfiguration* tls, const std::string& message)
    {
        const KiottyNetAddress address = loopbackAddress(port);
        const KiottyNetCode connected = KiottyNet_connectStreamClient(client, &address, tls);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connected);
        if (connected != KIOTTY_NET_CODE_SUCCESS)
        {
            return std::string();
        }
        return echoOver(client, message);
    }
}

TEST_F(DestinationTest, OneStreamClientMovesBetweenAPlainServerAndATlsServer)
{
    if (!tlsBuilt())
    {
        GTEST_SKIP() << "built without TLS";
    }
    const uint16_t plain_port = kiotty_test::unusedPort();
    const uint16_t secure_port = kiotty_test::unusedPort();
    KiottyNetServerHandle plain = createServer();
    KiottyNetServerHandle secure = createServer();
    const KiottyNetTlsServerConfiguration server_tls = serverTls();
    const KiottyNetTlsClientConfiguration client_tls = clientTls("ca.pem");
    App plain_app(Role::ECHO);
    App secure_app(Role::ECHO);
    RunningServer plain_running(plain, plain_port, nullptr, plain_app);
    RunningServer secure_running(secure, secure_port, &server_tls, secure_app);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(plain, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(secure, WAIT_MS));

    KiottyNetStreamClientHandle client = createStreamClient();
    EXPECT_EQ("abcd", connectAndEcho(client, plain_port, nullptr, "abcd"));
    EXPECT_EQ("efgh", connectAndEcho(client, secure_port, &client_tls, "efgh"));
    EXPECT_EQ("ijkl", connectAndEcho(client, plain_port, nullptr, "ijkl"));
    EXPECT_EQ("abcdijkl", plain_app.text());
    EXPECT_EQ("efgh", secure_app.text());

    KiottyNet_disconnectStreamClient(client);
    KiottyNet_destroyStreamClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, plain_running.stop());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, secure_running.stop());
    KiottyNet_destroyServer(context, plain);
    KiottyNet_destroyServer(context, secure);
}

TEST_F(DestinationTest, EachConnectUsesOnlyTheTlsConfigurationItWasGiven)
{
    if (!tlsBuilt())
    {
        GTEST_SKIP() << "built without TLS";
    }
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer();
    const KiottyNetTlsServerConfiguration server_tls = serverTls();
    App app(Role::ECHO);
    RunningServer running(server, port, &server_tls, app);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));

    KiottyNetStreamClientHandle client = createStreamClient();
    const KiottyNetTlsClientConfiguration trusted = clientTls("ca.pem");
    const KiottyNetTlsClientConfiguration stranger = clientTls("other_ca.pem");
    const KiottyNetAddress address = loopbackAddress(port);
    EXPECT_EQ("abcd", connectAndEcho(client, port, &trusted, "abcd"));
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_VERIFY_FAILED, KiottyNet_connectStreamClient(client, &address, &stranger));
    char byte = 0;
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, KiottyNet_readStreamClient(client, &byte, 1, 10).code);
    EXPECT_EQ("efgh", connectAndEcho(client, port, &trusted, "efgh"));

    KiottyNet_disconnectStreamClient(client);
    KiottyNet_destroyStreamClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyServer(context, server);
}

TEST_F(DestinationTest, AServerRunsAgainOnAnotherPortWithTlsAfterClose)
{
    if (!tlsBuilt())
    {
        GTEST_SKIP() << "built without TLS";
    }
    KiottyNetServerHandle server = createServer();
    KiottyNetStreamClientHandle client = createStreamClient();
    const uint16_t first_port = kiotty_test::unusedPort();
    {
        App app(Role::ECHO);
        RunningServer running(server, first_port, nullptr, app);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
        EXPECT_EQ("abcd", connectAndEcho(client, first_port, nullptr, "abcd"));
        KiottyNet_disconnectStreamClient(client);
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    }

    const uint16_t second_port = kiotty_test::unusedPort();
    const KiottyNetTlsServerConfiguration server_tls = serverTls();
    const KiottyNetTlsClientConfiguration client_tls = clientTls("ca.pem");
    App app(Role::ECHO);
    RunningServer running(server, second_port, &server_tls, app);
    ASSERT_TRUE(kiotty_test::waitUntil([&]() { return KiottyNet_awaitServer(server, 1) == KIOTTY_NET_CODE_SUCCESS; }, WAIT_MS));
    kiotty_test::LoopbackPeer stale;
    EXPECT_FALSE(stale.connectTcp(first_port));
    EXPECT_EQ("efgh", connectAndEcho(client, second_port, &client_tls, "efgh"));

    KiottyNet_disconnectStreamClient(client);
    KiottyNet_destroyStreamClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyServer(context, server);
}

TEST_F(DestinationTest, OneEventClientRunsPlainThenTls)
{
    if (!tlsBuilt())
    {
        GTEST_SKIP() << "built without TLS";
    }
    const uint16_t plain_port = kiotty_test::unusedPort();
    const uint16_t secure_port = kiotty_test::unusedPort();
    KiottyNetServerHandle plain = createServer();
    KiottyNetServerHandle secure = createServer();
    const KiottyNetTlsServerConfiguration server_tls = serverTls();
    const KiottyNetTlsClientConfiguration client_tls = clientTls("ca.pem");
    App plain_app(Role::ECHO);
    App secure_app(Role::ECHO);
    RunningServer plain_running(plain, plain_port, nullptr, plain_app);
    RunningServer secure_running(secure, secure_port, &server_tls, secure_app);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(plain, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(secure, WAIT_MS));

    KiottyNetEventClientHandle client = createEventClient();
    const KiottyNetAddress plain_address = loopbackAddress(plain_port);
    const KiottyNetAddress secure_address = loopbackAddress(secure_port);
    {
        App app(Role::PING);
        std::thread io([&]() { (void)KiottyNet_runEventClient(client, &plain_address, nullptr, &HANDLER, &app); });
        EXPECT_TRUE(kiotty_test::waitUntil([&]() { return app.read.load() > 0; }, WAIT_MS));
        EXPECT_EQ("ping", app.text());
        KiottyNet_closeEventClient(client);
        io.join();
    }
    {
        App app(Role::PING);
        std::thread io([&]() { (void)KiottyNet_runEventClient(client, &secure_address, &client_tls, &HANDLER, &app); });
        EXPECT_TRUE(kiotty_test::waitUntil([&]() { return app.read.load() > 0; }, WAIT_MS));
        EXPECT_EQ("ping", app.text());
        KiottyNet_closeEventClient(client);
        io.join();
    }
    EXPECT_EQ("ping", plain_app.text());
    EXPECT_EQ("ping", secure_app.text());

    KiottyNet_destroyEventClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, plain_running.stop());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, secure_running.stop());
    KiottyNet_destroyServer(context, plain);
    KiottyNet_destroyServer(context, secure);
}

TEST_F(DestinationTest, ABadAddressOrAnUninitializedTlsConfigurationFailsBeforeAnySocketOpens)
{
    KiottyNetServerHandle server = createServer();
    KiottyNetStreamClientHandle stream = createStreamClient();
    KiottyNetEventClientHandle event = createEventClient();
    App app(Role::ECHO);
    const KiottyNetAddress good = loopbackAddress(kiotty_test::unusedPort());
    KiottyNetAddress zero_port = good;
    zero_port.port = 0;
    KiottyNetAddress unknown = good;
    unknown.transport = static_cast<KiottyNetTransport>(7);
    KiottyNetAddress unterminated = good;
    std::memset(unterminated.host, 'a', sizeof(unterminated.host));
    const KiottyNetTlsServerConfiguration server_tls = {};
    const KiottyNetTlsClientConfiguration client_tls = {};
    const KiottyNetCode invalid = KIOTTY_NET_CODE_INVALID_ARGUMENT;

    EXPECT_EQ(invalid, KiottyNet_runServer(server, &zero_port, nullptr, &HANDLER, &app));
    EXPECT_EQ(invalid, KiottyNet_runServer(server, &unknown, nullptr, &HANDLER, &app));
    EXPECT_EQ(invalid, KiottyNet_runServer(server, &unterminated, nullptr, &HANDLER, &app));
    EXPECT_EQ(invalid, KiottyNet_runServer(server, &good, &server_tls, &HANDLER, &app));
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, KiottyNet_awaitServer(server, 1));
    kiotty_test::LoopbackPeer peer;
    EXPECT_FALSE(peer.connectTcp(good.port));

    EXPECT_EQ(invalid, KiottyNet_connectStreamClient(stream, &zero_port, nullptr));
    EXPECT_EQ(invalid, KiottyNet_connectStreamClient(stream, &unterminated, nullptr));
    EXPECT_EQ(invalid, KiottyNet_connectStreamClient(stream, &good, &client_tls));
    EXPECT_EQ(invalid, KiottyNet_runEventClient(event, &unknown, nullptr, &HANDLER, &app));
    EXPECT_EQ(invalid, KiottyNet_runEventClient(event, &good, &client_tls, &HANDLER, &app));
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, KiottyNet_awaitEventClient(event, 1));
    EXPECT_EQ(0, app.ready.load());

    KiottyNet_destroyEventClient(context, event);
    KiottyNet_destroyStreamClient(context, stream);
    KiottyNet_destroyServer(context, server);
}

TEST_F(DestinationTest, TlsIsNotSupportedWhenBuiltWithoutTls)
{
    if (tlsBuilt())
    {
        GTEST_SKIP() << "built with TLS";
    }
    KiottyNetServerHandle server = createServer();
    KiottyNetStreamClientHandle client = createStreamClient();
    App app(Role::ECHO);
    const KiottyNetAddress address = loopbackAddress(kiotty_test::unusedPort());
    const KiottyNetTlsServerConfiguration server_tls = serverTls();
    const KiottyNetTlsClientConfiguration client_tls = clientTls("ca.pem");
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED, KiottyNet_runServer(server, &address, &server_tls, &HANDLER, &app));
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED, KiottyNet_awaitServer(server, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_NOT_SUPPORTED, KiottyNet_connectStreamClient(client, &address, &client_tls));
    KiottyNet_destroyStreamClient(context, client);
    KiottyNet_destroyServer(context, server);
}

#endif

TEST(DestinationMemory, ServerMemorySizeCountsTheTlsStagesOnlyInATlsBuild)
{
    KiottyNetServerConfiguration small;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&small));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&small, 1));
    KiottyNetServerConfiguration large = small;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&large, 2));
    const size_t per_connection = KiottyNet_serverMemorySize(&large) - KiottyNet_serverMemorySize(&small);
    const size_t plain = small.receive_capacity + small.send_capacity + small.push_capacity;
    const size_t cipher = small.receive_capacity + small.send_capacity + small.push_capacity;
    if (tlsBuilt())
    {
        EXPECT_GT(per_connection, plain + cipher);
    }
    else
    {
        EXPECT_GT(per_connection, plain);
        EXPECT_LT(per_connection, plain + cipher);
    }
}
