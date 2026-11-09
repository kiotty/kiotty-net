#include "context/kiotty_context.h"

#include "context/kiotty_action.h"
#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_tls.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if KIOTTY_NET_HAS_EVENT_BACKEND

namespace net = kiotty::net::implementation;
using kiotty_test::LoopbackPeer;

namespace
{
    const uint32_t WAIT_MS = 5000;
    const uint32_t LONG_WAIT_MS = 60000;
    const uint32_t QUIET_MS = 200;
    const size_t   MESSAGE_BYTES = 4;
    const int      CHURN_THREADS = 4;
    const int      CHURN_PER_THREAD = 250;
    const char*    LOOPBACK_HOST = "127.0.0.1";

    enum class Role : uint8_t
    {
        ECHO,
        PING,
        WORK,
        DRAIN
    };

    struct Probe
    {
        explicit Probe(Role probe_role)
            : role(probe_role)
            , outgoing()
            , received()
            , connected(0)
            , ready(0)
            , read(0)
            , disconnected(0)
            , errors(0)
            , work_started(0)
            , stream_owner(nullptr)
            , stream_id(0)
        {
        }

        Role                                    role;
        std::string                             outgoing;
        std::string                             received;
        std::atomic<int>                        connected;
        std::atomic<int>                        ready;
        std::atomic<int>                        read;
        std::atomic<int>                        disconnected;
        std::atomic<int>                        errors;
        std::atomic<int>                        work_started;
        std::atomic<KiottyNetEndpoint*>  stream_owner;
        std::atomic<int64_t>                    stream_id;
    };

    Probe& probeOf(void* context)
    {
        return *static_cast<Probe*>(context);
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        Probe& probe = probeOf(app_context);
        probe.connected.fetch_add(1);
        return net::acceptConnection(app_context);
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)connection;
        Probe& probe = probeOf(context);
        probe.ready.fetch_add(1);
        switch (probe.role)
        {
        case Role::ECHO:
            return net::thenRead(MESSAGE_BYTES, 0);
        case Role::PING:
            probe.outgoing = "ping";
            return net::thenWrite(spanOf(probe.outgoing), 0);
        case Role::WORK:
            return net::thenWork();
        case Role::DRAIN:
            return net::thenRead(1, 0);
        }
        return net::thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR);
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)connection;
        Probe& probe = probeOf(context);
        probe.received.append(data.data, data.length);
        probe.read.fetch_add(1);
        if (probe.role == Role::ECHO)
        {
            probe.outgoing.assign(data.data, data.length);
            return net::thenWrite(spanOf(probe.outgoing), 0);
        }
        return net::thenHold(0);
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length)
    {
        (void)context;
        (void)connection;
        (void)length;
        return net::thenRead(MESSAGE_BYTES, 0);
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)context;
        (void)connection;
        return net::thenHold(0);
    }

    void onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        Probe& probe = probeOf(context);
        probe.stream_id.store(stream.id);
        probe.stream_owner.store(stream.owner);
        probe.work_started.fetch_add(1);
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
    {
        (void)context;
        (void)connection;
        (void)reason;
        probeOf(app_context).disconnected.fetch_add(1);
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)info;
        (void)code;
        (void)message;
        probeOf(app_context).errors.fetch_add(1);
    }

    const KiottyNetConnectionHandler CALLBACKS = { &onConnected, &onReady, &onRead, &onWritten, &onTimeout, &onWorkStarted, &onDisconnected, &onError };

    KiottyNetServerConfiguration serverConfiguration(uint16_t port)
    {
        KiottyNetServerConfiguration configuration{};
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTcpServerConfiguration(configuration, LOOPBACK_HOST, port));
        return configuration;
    }

    KiottyNetClientConfiguration clientConfiguration(uint16_t port)
    {
        KiottyNetClientConfiguration configuration{};
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTcpClientConfiguration(configuration, LOOPBACK_HOST, port));
        return configuration;
    }

    KiottyNetTlsServerConfiguration serverTls()
    {
        KiottyNetTlsServerConfiguration configuration{};
        std::string cert = kiotty_test::certPath("server.pem");
        std::string key = kiotty_test::certPath("server.key");
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTlsServerConfiguration(configuration, cert.c_str(), key.c_str(), nullptr));
        return configuration;
    }

    KiottyNetTlsClientConfiguration clientTls()
    {
        KiottyNetTlsClientConfiguration configuration{};
        std::string trust = kiotty_test::certPath("ca.pem");
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTlsClientConfiguration(configuration));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::configureTlsClientTrustStore(configuration, trust.c_str()));
        return configuration;
    }

    class RunningServer
    {
    public:
        RunningServer(KiottyNetServer& server, Probe& probe)
            : _server(server)
            , _code(KIOTTY_NET_CODE_SUCCESS)
            , _returned(false)
            , _thread()
        {
            _thread = std::thread([this, &probe]() {
                _code.store(_server.run(CALLBACKS, &probe));
                _returned.store(true);
            });
        }

        ~RunningServer()
        {
            stop();
        }

        RunningServer(const RunningServer&) = delete;
        RunningServer& operator=(const RunningServer&) = delete;

        KiottyNetCode stop()
        {
            _server.close();
            if (_thread.joinable())
            {
                _thread.join();
            }
            return _code.load();
        }

        bool returned() const
        {
            return _returned.load();
        }

    private:
        KiottyNetServer&               _server;
        std::atomic<KiottyNetCode> _code;
        std::atomic<bool>          _returned;
        std::thread                _thread;
    };

    class EndpointTest : public ::testing::Test
    {
    protected:
        EndpointTest()
            : context(nullptr)
        {
        }

        void SetUp() override
        {
            net::ContextResult opened = net::Context::open();
            ASSERT_TRUE(opened);
            context = &opened.value();
        }

        void TearDown() override
        {
            if (context != nullptr)
            {
                context->release();
            }
        }

        KiottyNetServer& createServer(uint16_t port, const KiottyNetTlsServerConfiguration* tls = nullptr)
        {
            KiottyNetServerConfiguration configuration = serverConfiguration(port);
            net::ServerResult created = context->createServer(configuration, tls);
            EXPECT_TRUE(created);
            return created.value();
        }

        net::Context* context;
    };

    bool waitForStream(Probe& probe)
    {
        return kiotty_test::waitUntil([&probe]() { return probe.work_started.load() > 0; }, WAIT_MS);
    }

    std::string readFromStream(net::Endpoint& owner, int64_t id, size_t length)
    {
        std::string text;
        char buffer[64];
        while (text.size() < length)
        {
            KiottyNetResult result = owner.readStream(id, net::Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
            if (result.code != KIOTTY_NET_CODE_SUCCESS)
            {
                break;
            }
            text.append(buffer, static_cast<size_t>(result.bytes));
        }
        return text;
    }

    std::string readFromClient(KiottyNetStreamClient& client, size_t length)
    {
        std::string text;
        char buffer[64];
        while (text.size() < length)
        {
            KiottyNetResult result = client.read(net::Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
            if (result.code != KIOTTY_NET_CODE_SUCCESS)
            {
                break;
            }
            text.append(buffer, static_cast<size_t>(result.bytes));
        }
        return text;
    }

    void exchangeOverStream(net::Context& context, KiottyNetServer& server, uint16_t port, const KiottyNetTlsClientConfiguration* tls)
    {
        Probe probe(Role::WORK);
        RunningServer running(server, probe);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));

        KiottyNetClientConfiguration configuration = clientConfiguration(port);
        net::StreamClientResult created = context.createStreamClient(configuration, tls);
        ASSERT_TRUE(created);
        KiottyNetStreamClient& client = created.value();
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect());
        ASSERT_TRUE(waitForStream(probe));
        net::Endpoint& owner = *probe.stream_owner.load();
        const int64_t id = probe.stream_id.load();
        EXPECT_EQ(static_cast<net::Endpoint*>(&server), &owner);

        EXPECT_EQ(4, client.write(spanOf("ping"), WAIT_MS).bytes);
        EXPECT_EQ("ping", readFromStream(owner, id, 4));
        EXPECT_TRUE(owner.isStreamOpen(id));
        EXPECT_EQ(4, owner.writeStream(id, spanOf("pong"), WAIT_MS).bytes);
        EXPECT_EQ("pong", readFromClient(client, 4));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, owner.closeStream(id, net::thenClose(KIOTTY_NET_CODE_SUCCESS)));

        char byte = 0;
        KiottyNetResult closed = client.read(net::Span<char>{ &byte, 1 }, WAIT_MS);
        EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, closed.code);
        EXPECT_TRUE(kiotty_test::waitUntil([&probe]() { return probe.disconnected.load() == 1; }, WAIT_MS));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
        context.destroy(client);
    }
}

TEST_F(EndpointTest, ServerAwaitTimesOutBeforeRunThenReportsRunningAndClosed)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, server.await(10));
    Probe probe(Role::ECHO);
    {
        RunningServer running(server, probe);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, server.run(CALLBACKS, &probe));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    }
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, server.await(WAIT_MS));
    server.close();
    {
        RunningServer again(server, probe);
        EXPECT_TRUE(kiotty_test::waitUntil([&]() { return server.await(1) == KIOTTY_NET_CODE_SUCCESS; }, WAIT_MS));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, again.stop());
    }
    context->destroy(server);
}

TEST_F(EndpointTest, ServerRunReturnsTheBindFailure)
{
    const uint16_t port = kiotty_test::unusedPort();
    LoopbackPeer holder;
    ASSERT_TRUE(holder.listenTcp(port));
    KiottyNetServer& server = createServer(port);
    Probe probe(Role::ECHO);
    const KiottyNetCode code = server.run(CALLBACKS, &probe);
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, code);
    EXPECT_NE(KIOTTY_NET_CODE_CLOSED, code);
    EXPECT_EQ(code, server.await(WAIT_MS));
    context->destroy(server);
}

TEST_F(EndpointTest, ServerAndEventClientLoopbackRoundTrip)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    Probe server_probe(Role::ECHO);
    RunningServer running(server, server_probe);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));

    KiottyNetClientConfiguration configuration = clientConfiguration(port);
    net::EventClientResult created = context->createEventClient(configuration, nullptr);
    ASSERT_TRUE(created);
    KiottyNetEventClient& client = created.value();
    EXPECT_EQ(0, client.connection().id);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, client.notify(client.connection().id, spanOf("note")));

    Probe client_probe(Role::PING);
    std::atomic<KiottyNetCode> client_code(KIOTTY_NET_CODE_SUCCESS);
    std::thread io([&]() { client_code.store(client.run(CALLBACKS, &client_probe)); });
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, client.await(WAIT_MS));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return client_probe.read.load() > 0; }, WAIT_MS));
    EXPECT_EQ("ping", client_probe.received);
    KiottyNetConnectionHandle handle = client.connection();
    EXPECT_EQ(static_cast<KiottyNetEndpoint*>(&client), handle.owner);
    EXPECT_NE(0, handle.id);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, client.notify(client.connection().id, spanOf("note")));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return server_probe.read.load() > 1; }, WAIT_MS));
    EXPECT_EQ("pingnote", server_probe.received);

    client.close();
    io.join();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, client_code.load());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, client.await(WAIT_MS));
    EXPECT_EQ(1, client_probe.disconnected.load());
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return server_probe.disconnected.load() == 1; }, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    context->destroy(client);
    context->destroy(server);
}

TEST_F(EndpointTest, EventClientRunReturnsTheConnectFailure)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetClientConfiguration configuration = clientConfiguration(port);
    net::EventClientResult created = context->createEventClient(configuration, nullptr);
    ASSERT_TRUE(created);
    Probe probe(Role::PING);
    const KiottyNetCode code = created.value().run(CALLBACKS, &probe);
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, code);
    EXPECT_EQ(0, probe.connected.load());
    EXPECT_EQ(code, created.value().await(WAIT_MS));
    context->destroy(created.value());
}

TEST_F(EndpointTest, ServerCloseWaitsForWorkerCloseStream)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    Probe probe(Role::WORK);
    RunningServer running(server, probe);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));

    LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    ASSERT_TRUE(waitForStream(probe));
    server.close();
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return probe.disconnected.load() == 1; }, WAIT_MS));
    std::this_thread::sleep_for(std::chrono::milliseconds(QUIET_MS));
    EXPECT_FALSE(running.returned());

    net::Endpoint& owner = *probe.stream_owner.load();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, owner.closeStream(probe.stream_id.load(), net::thenClose(KIOTTY_NET_CODE_SUCCESS)));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return running.returned(); }, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    context->destroy(server);
}

TEST_F(EndpointTest, StreamClientSharesConnectionStreamCode)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    exchangeOverStream(*context, server, port, nullptr);
    context->destroy(server);
}

TEST_F(EndpointTest, StreamClientSharesConnectionStreamCodeOverTls)
{
    if (!net::tlsSupported())
    {
        GTEST_SKIP() << "built without TLS";
    }
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetTlsServerConfiguration server_tls = serverTls();
    KiottyNetTlsClientConfiguration client_tls = clientTls();
    KiottyNetServer& server = createServer(port, &server_tls);
    exchangeOverStream(*context, server, port, &client_tls);
    context->destroy(server);
}

TEST_F(EndpointTest, StreamClientRefusesConnectionHandleCallsAndReadsBeforeConnect)
{
    KiottyNetClientConfiguration configuration = clientConfiguration(kiotty_test::unusedPort());
    net::StreamClientResult created = context->createStreamClient(configuration, nullptr);
    ASSERT_TRUE(created);
    KiottyNetStreamClient& client = created.value();
    char byte = 0;
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, client.read(net::Span<char>{ &byte, 1 }, 10).code);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, client.write(spanOf("x"), 10).code);
    net::Endpoint& owner = client;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, owner.notify(1, spanOf("x")));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, owner.readStream(1, net::Span<char>{ &byte, 1 }, 10).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, owner.writeStream(1, spanOf("x"), 10).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, owner.closeStream(1, net::thenClose(KIOTTY_NET_CODE_SUCCESS)));
    EXPECT_FALSE(owner.isStreamOpen(1));
    context->destroy(client);
}

TEST_F(EndpointTest, OnDisconnectedIsCalledExactlyOncePerAcceptedConnectionOverAThousandConnections)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    Probe probe(Role::DRAIN);
    RunningServer running(server, probe);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));

    std::atomic<int> refused(0);
    std::vector<std::thread> clients;
    for (int t = 0; t < CHURN_THREADS; ++t)
    {
        clients.emplace_back([&]() {
            for (int i = 0; i < CHURN_PER_THREAD; ++i)
            {
                LoopbackPeer peer;
                if (!peer.connectTcp(port))
                {
                    refused.fetch_add(1);
                }
            }
        });
    }
    for (std::thread& client : clients)
    {
        client.join();
    }
    const int total = CHURN_THREADS * CHURN_PER_THREAD;
    EXPECT_EQ(0, refused.load());
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return probe.disconnected.load() >= total; }, LONG_WAIT_MS));
    std::this_thread::sleep_for(std::chrono::milliseconds(QUIET_MS));
    EXPECT_EQ(total, probe.connected.load());
    EXPECT_EQ(total, probe.ready.load());
    EXPECT_EQ(total, probe.disconnected.load());
    EXPECT_EQ(0, probe.errors.load());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    EXPECT_EQ(total, probe.disconnected.load());
    context->destroy(server);
}

namespace
{
    struct Reentry
    {
        Reentry(net::Context& owner_context, uint16_t port, bool destroy_on_connect)
            : context(owner_context)
            , server(nullptr)
            , client(clientConfiguration(port))
            , is_self_destroying(destroy_on_connect)
            , connected(0)
            , disconnected(0)
            , created(0)
        {
        }

        net::Context&                context;
        KiottyNetServer*                 server;
        KiottyNetClientConfiguration client;
        bool                         is_self_destroying;
        std::atomic<int>             connected;
        std::atomic<int>             disconnected;
        std::atomic<int>             created;
    };

    Reentry& reentryOf(void* context)
    {
        return *static_cast<Reentry*>(context);
    }

    KiottyNetConnectionDecision onReentryConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        Reentry& reentry = reentryOf(app_context);
        reentry.connected.fetch_add(1);
        if (reentry.is_self_destroying)
        {
            reentry.context.destroy(*reentry.server);
        }
        return net::acceptConnection(app_context);
    }

    KiottyNetConnectionAction onReentryReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)context;
        (void)connection;
        return net::thenRead(1, 0);
    }

    KiottyNetConnectionAction onReentryRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)context;
        (void)connection;
        (void)data;
        return net::thenHold(0);
    }

    void onReentryWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        (void)context;
        (void)stream;
    }

    void onReentryDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
    {
        (void)context;
        (void)connection;
        (void)reason;
        Reentry& reentry = reentryOf(app_context);
        reentry.disconnected.fetch_add(1);
        net::EventClientResult created = reentry.context.createEventClient(reentry.client, nullptr);
        if (created)
        {
            reentry.created.fetch_add(1);
            reentry.context.destroy(created.value());
        }
    }

    const KiottyNetConnectionHandler REENTRY_CALLBACKS = { &onReentryConnected, &onReentryReady, &onReentryRead, &onWritten, &onTimeout, &onReentryWorkStarted, &onReentryDisconnected, &onError };
}

TEST_F(EndpointTest, RetiredServerStartsFreshOnTheNextCreate)
{
    // generations of a reused slot are checked in kiotty_test_lazy_reusable_pool
    const uint16_t first_port = kiotty_test::unusedPort();
    KiottyNetServer& first = createServer(first_port);
    {
        Probe probe(Role::ECHO);
        RunningServer running(first, probe);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, first.await(WAIT_MS));
        LoopbackPeer peer;
        ASSERT_TRUE(peer.connectTcp(first_port));
        EXPECT_TRUE(kiotty_test::waitUntil([&]() { return probe.ready.load() == 1; }, WAIT_MS));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    }
    context->destroy(first);

    const uint16_t second_port = kiotty_test::unusedPort();
    KiottyNetServer& second = createServer(second_port);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, second.await(QUIET_MS));
    Probe probe(Role::ECHO);
    RunningServer running(second, probe);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, second.await(WAIT_MS));
    LoopbackPeer stale;
    EXPECT_FALSE(stale.connectTcp(first_port));
    LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(second_port));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return probe.ready.load() == 1; }, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    EXPECT_EQ(1, probe.connected.load());
    EXPECT_EQ(1, probe.disconnected.load());
    context->destroy(second);
}

TEST_F(EndpointTest, DestroyDoesNotBlockAWorkerThatCallsContextBeforeCloseStream)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    Probe probe(Role::WORK);
    RunningServer running(server, probe);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));
    LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    ASSERT_TRUE(waitForStream(probe));
    net::Endpoint& owner = *probe.stream_owner.load();
    const int64_t id = probe.stream_id.load();

    std::atomic<bool> destroyed(false);
    std::thread destroyer([&]() {
        context->destroy(server);
        destroyed.store(true);
    });
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return probe.disconnected.load() == 1; }, WAIT_MS));
    std::this_thread::sleep_for(std::chrono::milliseconds(QUIET_MS));
    EXPECT_FALSE(destroyed.load());

    KiottyNetClientConfiguration configuration = clientConfiguration(port);
    net::EventClientResult created = context->createEventClient(configuration, nullptr);
    EXPECT_TRUE(created);
    if (created)
    {
        context->destroy(created.value());
    }
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, owner.closeStream(id, net::thenClose(KIOTTY_NET_CODE_SUCCESS)));
    destroyer.join();
    EXPECT_TRUE(destroyed.load());
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return running.returned(); }, WAIT_MS));
}

TEST_F(EndpointTest, DestroyDoesNotBlockALoopCallbackThatCallsContext)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    Reentry reentry(*context, port, false);
    reentry.server = &server;
    std::thread io([&]() { (void)server.run(REENTRY_CALLBACKS, &reentry); });
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));
    LoopbackPeer peer;
    EXPECT_TRUE(peer.connectTcp(port));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return reentry.connected.load() == 1; }, WAIT_MS));
    context->destroy(server);
    io.join();
    EXPECT_EQ(1, reentry.disconnected.load());
    EXPECT_EQ(1, reentry.created.load());
}

TEST_F(EndpointTest, ServerDestroyedFromItsOwnCallbackIsReclaimedByTheNextCreate)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServer& server = createServer(port);
    Reentry reentry(*context, port, true);
    reentry.server = &server;
    std::thread io([&]() { (void)server.run(REENTRY_CALLBACKS, &reentry); });
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, server.await(WAIT_MS));
    LoopbackPeer peer;
    const bool is_connected = peer.connectTcp(port);
    EXPECT_TRUE(is_connected);
    const bool is_destroyed = kiotty_test::waitUntil([&]() { return reentry.connected.load() == 1; }, WAIT_MS);
    EXPECT_TRUE(is_destroyed);
    if (!is_connected || !is_destroyed)
    {
        server.close();
    }
    io.join();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, server.await(WAIT_MS));

    net::ServerResult next = context->createServer(serverConfiguration(kiotty_test::unusedPort()), nullptr);
    ASSERT_TRUE(next);
    EXPECT_EQ(&server, &next.value());
    context->destroy(next.value());
}

#endif
