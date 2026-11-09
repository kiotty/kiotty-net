#include "kiotty_net_base.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace
{
    const uint32_t WAIT_MS = 5000;
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const size_t   MESSAGE_BYTES = 4;

    enum class Role : uint8_t
    {
        ECHO,
        PING,
        WORK
    };

    struct App
    {
        explicit App(Role app_role)
            : role(app_role)
            , ready(0)
            , read(0)
            , disconnected(0)
            , work_started(0)
            , busy_code(KIOTTY_NET_CODE_SUCCESS)
            , handle_owner(nullptr)
            , handle_id(0)
            , stream_owner(nullptr)
            , stream_id(0)
            , outgoing()
            , received()
        {
        }

        Role                            role;
        std::atomic<int>                ready;
        std::atomic<int>                read;
        std::atomic<int>                disconnected;
        std::atomic<int>                work_started;
        std::atomic<KiottyNetCode>      busy_code;
        std::atomic<KiottyNetEndpoint*> handle_owner;
        std::atomic<int64_t>            handle_id;
        std::atomic<KiottyNetEndpoint*> stream_owner;
        std::atomic<int64_t>            stream_id;
        std::string                     outgoing;
        std::string                     received;

        KiottyNetConnectionHandle handle() const
        {
            return KiottyNetConnectionHandle{ handle_owner.load(), handle_id.load() };
        }

        KiottyNetConnectionStream stream() const
        {
            return KiottyNetConnectionStream{ stream_owner.load(), stream_id.load() };
        }
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
        App& app = appOf(context);
        app.handle_owner.store(connection.owner);
        app.handle_id.store(connection.id);
        app.ready.fetch_add(1);
        switch (app.role)
        {
        case Role::ECHO:
            return KiottyNet_thenRead(MESSAGE_BYTES, 0);
        case Role::PING:
            app.outgoing = "ping";
            return KiottyNet_thenWrite(spanOf(app.outgoing), 0);
        case Role::WORK:
            return KiottyNet_thenWork();
        }
        return KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR);
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)connection;
        App& app = appOf(context);
        app.received.append(data.data, data.length);
        app.read.fetch_add(1);
        if (app.role == Role::ECHO)
        {
            app.outgoing.assign(data.data, data.length);
            return KiottyNet_thenWrite(spanOf(app.outgoing), 0);
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
        App& app = appOf(context);
        char byte = 0;
        KiottyNetResult busy = KiottyNet_readConnectionStream(stream, &byte, 1, 0);
        app.busy_code.store(busy.code);
        app.stream_owner.store(stream.owner);
        app.stream_id.store(stream.id);
        app.work_started.fetch_add(1);
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
    {
        (void)context;
        (void)connection;
        (void)reason;
        appOf(app_context).disconnected.fetch_add(1);
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)app_context;
        (void)info;
        (void)code;
        (void)message;
    }

    const KiottyNetConnectionHandler HANDLER = { &onConnected, &onReady, &onRead, &onWritten, &onTimeout, &onWorkStarted, &onDisconnected, &onError };

    class RunningServer
    {
    public:
        RunningServer(KiottyNetServerHandle server, App& app)
            : _server(server)
            , _code(KIOTTY_NET_CODE_SUCCESS)
            , _thread([this, &app]() { _code.store(KiottyNet_runServer(_server, &HANDLER, &app)); })
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
        std::atomic<KiottyNetCode> _code;
        std::thread                _thread;
    };

    class BaseApiTest : public ::testing::Test
    {
    protected:
        BaseApiTest()
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

        KiottyNetServerHandle createServer(uint16_t port)
        {
            KiottyNetServerConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpServerConfiguration(&configuration, LOOPBACK_HOST, port));
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&configuration, 4));
            KiottyNetServerResult created = KiottyNet_createServer(context, &configuration, nullptr);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        KiottyNetClientConfiguration clientConfiguration(uint16_t port)
        {
            KiottyNetClientConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpClientConfiguration(&configuration, LOOPBACK_HOST, port));
            return configuration;
        }

        KiottyNetContext context;
    };
}

TEST_F(BaseApiTest, InitializeContextTwiceReturnsTheSameHandle)
{
    KiottyNetContextResult again = KiottyNet_initializeContext();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ(context, again.handle);
}

TEST_F(BaseApiTest, NullArgumentsAreRejectedWithoutTouchingAnything)
{
    const KiottyNetCode invalid = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    KiottyNetServerConfiguration server_configuration;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpServerConfiguration(&server_configuration, LOOPBACK_HOST, kiotty_test::unusedPort()));
    KiottyNetClientConfiguration client_configuration = clientConfiguration(9);

    EXPECT_EQ(invalid, KiottyNet_initializeTcpServerConfiguration(nullptr, LOOPBACK_HOST, 0));
    EXPECT_EQ(invalid, KiottyNet_configureServerMaxClients(nullptr, 1));
    EXPECT_EQ(invalid, KiottyNet_initializeTcpClientConfiguration(nullptr, LOOPBACK_HOST, 0));
    EXPECT_EQ(invalid, KiottyNet_initializeTlsServerConfiguration(nullptr, "a", "b", nullptr));
    EXPECT_EQ(invalid, KiottyNet_initializeTlsClientConfiguration(nullptr));

    EXPECT_EQ(invalid, KiottyNet_createServer(nullptr, &server_configuration, nullptr).code);
    EXPECT_EQ(invalid, KiottyNet_createServer(context, nullptr, nullptr).code);
    EXPECT_EQ(invalid, KiottyNet_createEventClient(nullptr, &client_configuration, nullptr).code);
    EXPECT_EQ(invalid, KiottyNet_createStreamClient(context, nullptr, nullptr).code);
    EXPECT_EQ(nullptr, KiottyNet_createServer(context, nullptr, nullptr).handle);

    EXPECT_EQ(invalid, KiottyNet_runServer(nullptr, &HANDLER, nullptr));
    EXPECT_EQ(invalid, KiottyNet_awaitServer(nullptr, 0));
    EXPECT_EQ(invalid, KiottyNet_broadcastServer(nullptr, spanOf("x")));
    EXPECT_EQ(invalid, KiottyNet_runEventClient(nullptr, &HANDLER, nullptr));
    EXPECT_EQ(invalid, KiottyNet_awaitEventClient(nullptr, 0));
    EXPECT_EQ(invalid, KiottyNet_notifyEventClient(nullptr, spanOf("x")));
    EXPECT_EQ(nullptr, KiottyNet_getEventClientConnection(nullptr).owner);
    EXPECT_EQ(invalid, KiottyNet_connectStreamClient(nullptr));
    EXPECT_EQ(invalid, KiottyNet_readStreamClient(nullptr, nullptr, 0, 0).code);
    EXPECT_EQ(invalid, KiottyNet_writeStreamClient(nullptr, spanOf("x"), 0).code);

    KiottyNetServerResult created = KiottyNet_createServer(context, &server_configuration, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
    EXPECT_EQ(invalid, KiottyNet_runServer(created.handle, nullptr, nullptr));

    KiottyNetConnectionHandle no_owner = { nullptr, 0 };
    KiottyNetConnectionStream no_stream = { nullptr, 0 };
    char byte = 0;
    EXPECT_EQ(invalid, KiottyNet_notifyConnection(no_owner, spanOf("x")));
    EXPECT_EQ(0, KiottyNet_getConnectionInfo(no_owner).port);
    EXPECT_EQ(invalid, KiottyNet_readConnectionStream(no_stream, &byte, 1, 0).code);
    EXPECT_EQ(invalid, KiottyNet_writeConnectionStream(no_stream, spanOf("x"), 0).code);
    EXPECT_EQ(invalid, KiottyNet_writeConnectionStreamFile(no_stream, "file", 0, 0, 0));
    EXPECT_FALSE(KiottyNet_isConnectionStreamOpen(no_stream));
    EXPECT_EQ(invalid, KiottyNet_closeConnectionStream(no_stream, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS)));

    KiottyNet_closeServer(nullptr);
    KiottyNet_closeEventClient(nullptr);
    KiottyNet_disconnectStreamClient(nullptr);
    KiottyNet_destroyServer(nullptr, created.handle);
    KiottyNet_destroyServer(context, nullptr);
    KiottyNet_destroyEventClient(context, nullptr);
    KiottyNet_destroyStreamClient(context, nullptr);
    KiottyNet_releaseContext(nullptr);
    KiottyNet_destroyServer(context, created.handle);
}

#if defined(_WIN32) || defined(__linux__)

namespace
{
    const int64_t RANDOM_ID = 0x1234567890LL;

    bool receive(kiotty_test::LoopbackPeer& peer, const std::string& expected)
    {
        std::string text(expected.size(), '\0');
        if (!peer.recvExactly(&text[0], text.size(), WAIT_MS))
        {
            return false;
        }
        return text == expected;
    }
}

TEST_F(BaseApiTest, NotifyAnswersEveryKindOfHandle)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    App app(Role::ECHO);
    RunningServer running(server, app);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));

    KiottyNetConnectionHandle first = { nullptr, 0 };
    {
        kiotty_test::LoopbackPeer peer;
        ASSERT_TRUE(peer.connectTcp(port));
        ASSERT_TRUE(kiotty_test::waitUntil([&]() { return app.ready.load() == 1; }, WAIT_MS));
        first = app.handle();
        EXPECT_EQ(static_cast<void*>(server), static_cast<void*>(first.owner));

        KiottyNetConnectionHandle no_owner = { nullptr, 0 };
        KiottyNetConnectionHandle zero_id = { first.owner, 0 };
        KiottyNetConnectionHandle random_id = { first.owner, RANDOM_ID };
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_notifyConnection(no_owner, spanOf("note")));
        EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_notifyConnection(zero_id, spanOf("note")));
        EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_notifyConnection(random_id, spanOf("note")));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_notifyConnection(first, spanOf("note")));
        EXPECT_TRUE(receive(peer, "note"));
        EXPECT_STREQ(LOOPBACK_HOST, KiottyNet_getConnectionInfo(first).hostname);
    }
    ASSERT_TRUE(kiotty_test::waitUntil([&]() { return app.disconnected.load() == 1; }, WAIT_MS));

    kiotty_test::LoopbackPeer second_peer;
    ASSERT_TRUE(second_peer.connectTcp(port));
    ASSERT_TRUE(kiotty_test::waitUntil([&]() { return app.ready.load() == 2; }, WAIT_MS));
    KiottyNetConnectionHandle second = app.handle();
    EXPECT_NE(first.id, second.id);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_notifyConnection(first, spanOf("note")));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_notifyConnection(second, spanOf("note")));
    EXPECT_TRUE(receive(second_peer, "note"));

    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyServer(context, server);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_notifyConnection(second, spanOf("note")));
}

TEST_F(BaseApiTest, ServerAndEventClientRoundTripThroughTheCApi)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    App server_app(Role::ECHO);
    RunningServer running(server, server_app);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));

    KiottyNetClientConfiguration configuration = clientConfiguration(port);
    KiottyNetEventClientResult created = KiottyNet_createEventClient(context, &configuration, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
    KiottyNetEventClientHandle client = created.handle;
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, KiottyNet_awaitEventClient(client, 1));

    App client_app(Role::PING);
    std::atomic<KiottyNetCode> client_code(KIOTTY_NET_CODE_SUCCESS);
    std::thread io([&]() { client_code.store(KiottyNet_runEventClient(client, &HANDLER, &client_app)); });
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitEventClient(client, WAIT_MS));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return client_app.read.load() > 0; }, WAIT_MS));
    EXPECT_EQ("ping", client_app.received);
    KiottyNetConnectionHandle connection = KiottyNet_getEventClientConnection(client);
    EXPECT_NE(0, connection.id);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_notifyEventClient(client, spanOf("note")));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return server_app.read.load() > 1; }, WAIT_MS));
    EXPECT_EQ("pingnote", server_app.received);

    KiottyNet_closeEventClient(client);
    io.join();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, client_code.load());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyEventClient(context, client);
    KiottyNet_destroyServer(context, server);
}

TEST_F(BaseApiTest, StreamClientTalksToAWorkerAndTheIoThreadGetsBusy)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    App app(Role::WORK);
    RunningServer running(server, app);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));

    KiottyNetClientConfiguration configuration = clientConfiguration(port);
    KiottyNetStreamClientResult created = KiottyNet_createStreamClient(context, &configuration, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
    KiottyNetStreamClientHandle client = created.handle;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectStreamClient(client));
    ASSERT_TRUE(kiotty_test::waitUntil([&]() { return app.work_started.load() == 1; }, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, app.busy_code.load());

    KiottyNetConnectionStream stream = app.stream();
    EXPECT_TRUE(KiottyNet_isConnectionStreamOpen(stream));
    KiottyNetResult written = KiottyNet_writeStreamClient(client, spanOf("hello"), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, written.code);
    char buffer[16] = {};
    KiottyNetResult read = KiottyNet_readConnectionStream(stream, buffer, 5, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, read.code);
    EXPECT_EQ("hello", std::string(buffer, static_cast<size_t>(read.bytes)));

    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeConnectionStream(stream, spanOf("world"), WAIT_MS).code);
    char reply[16] = {};
    KiottyNetResult answered = KiottyNet_readStreamClient(client, reply, 5, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ("world", std::string(reply, static_cast<size_t>(answered.bytes)));

    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_closeConnectionStream(stream, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS)));
    KiottyNet_disconnectStreamClient(client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyStreamClient(context, client);
    KiottyNet_destroyServer(context, server);
}

#endif

TEST_F(BaseApiTest, MemorySizesCoverTheObjects)
{
    EXPECT_GT(KiottyNet_serverMemorySize(), KiottyNet_clientMemorySize());
    EXPECT_GT(KiottyNet_clientMemorySize(), 0u);
}
