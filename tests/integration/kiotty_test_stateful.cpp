#include "kiotty_net_statemachine.h"

#include "kiotty_test_stateful_c.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;
    const size_t   HEADER_BYTES = 4;
    const size_t   MAX_BODY = 64;
    const size_t   SESSIONS = 4;

    uint32_t decodeLength(KiottyNetSpan header)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(header.data);
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    void encodeLength(char* header, uint32_t length)
    {
        header[0] = char(length >> 24);
        header[1] = char(length >> 16);
        header[2] = char(length >> 8);
        header[3] = char(length);
    }

    struct Errors
    {
        std::mutex                 mutex;
        std::vector<KiottyNetCode> codes;

        void add(KiottyNetCode code)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            codes.push_back(code);
        }

        size_t size()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return codes.size();
        }

        bool has(KiottyNetCode code)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            for (KiottyNetCode seen : codes)
            {
                if (seen == code)
                {
                    return true;
                }
            }
            return false;
        }
    };

    enum class ServerState
    {
        ReadingHeader,
        ReadingBody,
        Writing,
        Working,
        ErrorOccurred,
        Closed
    };

    struct ServerSession
    {
        char                      frame[HEADER_BYTES + MAX_BODY];
        size_t                    frame_length;
        bool                      in_use;
        KiottyNetConnectionStream stream;
    };

    struct ServerApp
    {
        ServerSession                          session[SESSIONS] = {};
        bool                                   exhausted = false;
        std::atomic<int>                       created{ 0 };
        std::atomic<int>                       released{ 0 };
        std::atomic<int>                       closed{ 0 };
        std::atomic<KiottyNetConnectionStream*> handed{ nullptr };
        Errors                                 errors;
    };

    typedef KiottyNet_Transition<ServerState> ServerNext;

    ServerSession& serverSession(KiottyNetConnectionContext context)
    {
        return *static_cast<ServerSession*>(context);
    }

    ServerApp* g_server = nullptr;

    KiottyNetTransition serverReadHeader()
    {
        return ServerNext::moveToNextState(ServerState::ReadingHeader, KiottyNet_thenRead(HEADER_BYTES, WAIT_MS));
    }

    KiottyNetTransition serverOnConnected(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return serverReadHeader();
    }

    KiottyNetTransition serverOnHeader(KiottyNetConnectionContext context, KiottyNetConnectionHandle, KiottyNetConnectionEvent event)
    {
        ServerSession& s = serverSession(context);
        const uint32_t length = decodeLength(event.data);
        if (length == 0 || length > MAX_BODY)
        {
            return ServerNext::moveToNextState(ServerState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR));
        }
        std::memcpy(s.frame, event.data.data, HEADER_BYTES);
        s.frame_length = HEADER_BYTES + length;
        return ServerNext::moveToNextState(ServerState::ReadingBody, KiottyNet_thenRead(length, WAIT_MS));
    }

    KiottyNetTransition serverOnBody(KiottyNetConnectionContext context, KiottyNetConnectionHandle, KiottyNetConnectionEvent event)
    {
        ServerSession& s = serverSession(context);
        std::memcpy(s.frame + HEADER_BYTES, event.data.data, event.data.length);
        const KiottyNetSpan frame = { s.frame, s.frame_length };
        return ServerNext::moveToNextState(ServerState::Writing, KiottyNet_thenWrite(frame, WAIT_MS));
    }

    KiottyNetTransition serverOnWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return serverReadHeader();
    }

    KiottyNetTransition serverOnTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ServerNext::moveToNextState(ServerState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_TIMEOUT));
    }

    KiottyNetTransition serverOnClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        g_server->closed.fetch_add(1);
        return ServerNext::moveToNextState(ServerState::Closed, KiottyNet_thenHold(0));
    }

    KiottyNetTransition serverShortRead(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ServerNext::moveToNextState(ServerState::ReadingHeader, KiottyNet_thenRead(HEADER_BYTES, 200));
    }

    KiottyNetTransition serverStartWork(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ServerNext::moveToNextState(ServerState::Working, KiottyNet_thenWork());
    }

    KiottyNetTransition serverOnWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionHandle, KiottyNetConnectionEvent event)
    {
        ServerSession& s = serverSession(context);
        s.stream = event.stream;
        g_server->handed.store(&s.stream);
        return ServerNext::moveToNextState(ServerState::Working, KiottyNet_thenWork());
    }

    KiottyNetTransition serverAfterWork(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ServerNext::moveToNextState(ServerState::Closed, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS));
    }

    KiottyNetConnectionContext serverAcquire(KiottyNetAppContext app_context, KiottyNetConnectionInfo)
    {
        ServerApp& app = *static_cast<ServerApp*>(app_context);
        app.created.fetch_add(1);
        if (app.exhausted)
        {
            return nullptr;
        }
        for (ServerSession& s : app.session)
        {
            if (!s.in_use)
            {
                s = ServerSession();
                s.in_use = true;
                return &s;
            }
        }
        return nullptr;
    }

    void serverRelease(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle)
    {
        serverSession(context).in_use = false;
        static_cast<ServerApp*>(app_context)->released.fetch_add(1);
    }

    void serverOnError(KiottyNetAppContext app_context, KiottyNetConnectionInfo, KiottyNetCode code, const char*)
    {
        static_cast<ServerApp*>(app_context)->errors.add(code);
    }

    constexpr auto ECHO_RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(serverOnConnected).to(ServerState::ReadingHeader),
        KiottyNet_from(ServerState::ReadingHeader).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(serverOnHeader).to(ServerState::ReadingBody, ServerState::ErrorOccurred),
        KiottyNet_from(ServerState::ReadingBody).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(serverOnBody).to(ServerState::Writing),
        KiottyNet_from(ServerState::Writing).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN).then(serverOnWritten).to(ServerState::ReadingHeader),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT).then(serverOnTimeout),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(serverOnClosed));
    constexpr KiottyNetStateMachine ECHO_MACHINE = KiottyNet_makeStateMachine(ECHO_RULES, ServerState::ReadingHeader);

    constexpr auto NO_TIMEOUT_RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(serverShortRead).to(ServerState::ReadingHeader),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(serverOnClosed));
    constexpr KiottyNetStateMachine NO_TIMEOUT_MACHINE = KiottyNet_makeStateMachine(NO_TIMEOUT_RULES, ServerState::ReadingHeader);

    constexpr auto WORK_RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(serverStartWork).to(ServerState::Working),
        KiottyNet_from(ServerState::Working).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED).then(serverOnWorkStarted).to(ServerState::Working),
        KiottyNet_from(ServerState::Working).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN).then(serverAfterWork).to(ServerState::Closed),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(serverOnClosed));
    constexpr KiottyNetStateMachine WORK_MACHINE = KiottyNet_makeStateMachine(WORK_RULES, ServerState::Working);

    enum class ClientState
    {
        Sending,
        ReadingHeader,
        ReadingBody,
        ErrorOccurred,
        Closed
    };

    struct ClientSession
    {
        char   frame[HEADER_BYTES + MAX_BODY];
        size_t frame_length;
        bool   matched;
        int    released;
    };

    typedef KiottyNet_Transition<ClientState> ClientNext;

    ClientSession& clientSession(KiottyNetConnectionContext context)
    {
        return *static_cast<ClientSession*>(context);
    }

    KiottyNetTransition clientOnConnected(KiottyNetConnectionContext context, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        ClientSession& s = clientSession(context);
        const KiottyNetSpan frame = { s.frame, s.frame_length };
        return ClientNext::moveToNextState(ClientState::Sending, KiottyNet_thenWrite(frame, WAIT_MS));
    }

    KiottyNetTransition clientOnWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ClientNext::moveToNextState(ClientState::ReadingHeader, KiottyNet_thenRead(HEADER_BYTES, WAIT_MS));
    }

    KiottyNetTransition clientOnHeader(KiottyNetConnectionContext context, KiottyNetConnectionHandle, KiottyNetConnectionEvent event)
    {
        ClientSession& s = clientSession(context);
        const uint32_t length = decodeLength(event.data);
        if (length != s.frame_length - HEADER_BYTES)
        {
            return ClientNext::moveToNextState(ClientState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR));
        }
        return ClientNext::moveToNextState(ClientState::ReadingBody, KiottyNet_thenRead(length, WAIT_MS));
    }

    KiottyNetTransition clientOnBody(KiottyNetConnectionContext context, KiottyNetConnectionHandle, KiottyNetConnectionEvent event)
    {
        ClientSession& s = clientSession(context);
        s.matched = std::memcmp(s.frame + HEADER_BYTES, event.data.data, event.data.length) == 0;
        return ClientNext::moveToNextState(ClientState::Closed, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS));
    }

    KiottyNetTransition clientOnTimeout(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ClientNext::moveToNextState(ClientState::ErrorOccurred, KiottyNet_thenClose(KIOTTY_NET_CODE_TIMEOUT));
    }

    KiottyNetTransition clientOnClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return ClientNext::moveToNextState(ClientState::Closed, KiottyNet_thenHold(0));
    }

    KiottyNetConnectionContext clientSingleSession(KiottyNetAppContext app_context, KiottyNetConnectionInfo)
    {
        return app_context;
    }

    void clientRelease(KiottyNetAppContext app_context, KiottyNetConnectionContext, KiottyNetConnectionHandle)
    {
        ++static_cast<ClientSession*>(app_context)->released;
    }

    constexpr auto CLIENT_RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(clientOnConnected).to(ClientState::Sending),
        KiottyNet_from(ClientState::Sending).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN).then(clientOnWritten).to(ClientState::ReadingHeader),
        KiottyNet_from(ClientState::ReadingHeader).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(clientOnHeader).to(ClientState::ReadingBody, ClientState::ErrorOccurred),
        KiottyNet_from(ClientState::ReadingBody).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(clientOnBody).to(ClientState::Closed),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT).then(clientOnTimeout),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(clientOnClosed));
    constexpr KiottyNetStateMachine CLIENT_MACHINE = KiottyNet_makeStateMachine(CLIENT_RULES, ClientState::Sending);

    class RunningStatefulServer
    {
    public:
        RunningStatefulServer(KiottyNetServerHandle server, const KiottyNetStatefulAppContext& stateful)
            : _server(server)
            , _code(KIOTTY_NET_CODE_SUCCESS)
            , _thread([this, &stateful]() { _code.store(KiottyNet_runStatefulServer(_server, &stateful)); })
        {
        }

        ~RunningStatefulServer()
        {
            stop();
        }

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

    class StatefulTest : public ::testing::Test
    {
    protected:
        StatefulTest()
            : context(nullptr)
            , app()
        {
            g_server = &app;
        }

        ~StatefulTest() override
        {
            g_server = nullptr;
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
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&configuration, SESSIONS));
            KiottyNetServerResult created = KiottyNet_createServer(context, &configuration, nullptr);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        KiottyNetEventClientHandle createEventClient(uint16_t port)
        {
            KiottyNetClientConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpClientConfiguration(&configuration, LOOPBACK_HOST, port));
            KiottyNetEventClientResult created = KiottyNet_createEventClient(context, &configuration, nullptr);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        KiottyNetStreamClientHandle connectStreamClient(uint16_t port)
        {
            KiottyNetClientConfiguration configuration;
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpClientConfiguration(&configuration, LOOPBACK_HOST, port));
            KiottyNetStreamClientResult created = KiottyNet_createStreamClient(context, &configuration, nullptr);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectStreamClient(created.handle));
            return created.handle;
        }

        KiottyNetStatefulAppContext serverStateful(const KiottyNetStateMachine& machine)
        {
            return KiottyNetStatefulAppContext{ &app, &machine, serverAcquire, serverRelease, serverOnError };
        }

        bool echoOnce(uint16_t port, const std::string& message)
        {
            ClientSession session = {};
            encodeLength(session.frame, static_cast<uint32_t>(message.size()));
            std::memcpy(session.frame + HEADER_BYTES, message.data(), message.size());
            session.frame_length = HEADER_BYTES + message.size();
            const KiottyNetStatefulAppContext stateful = { &session, &CLIENT_MACHINE, clientSingleSession, clientRelease, nullptr };
            KiottyNetEventClientHandle client = createEventClient(port);
            const KiottyNetCode code = KiottyNet_runStatefulEventClient(client, &stateful);
            KiottyNet_destroyEventClient(context, client);
            EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, code);
            EXPECT_EQ(1, session.released);
            return session.matched;
        }

        KiottyNetContext context;
        ServerApp        app;
    };
}

TEST_F(StatefulTest, StatefulEchoRoundTrip)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    const KiottyNetStatefulAppContext stateful = serverStateful(ECHO_MACHINE);
    {
        RunningStatefulServer running(server, stateful);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
        EXPECT_TRUE(echoOnce(port, "hello stateful"));
        EXPECT_TRUE(echoOnce(port, "again"));
        EXPECT_TRUE(kiotty_test::waitUntil([&]() { return app.released.load() == 2; }, WAIT_MS));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    }
    EXPECT_EQ(2, app.created.load());
    EXPECT_EQ(2, app.closed.load());
    EXPECT_EQ(2, app.released.load());
    KiottyNet_destroyServer(context, server);
}

TEST_F(StatefulTest, StatefulEchoRoundTripInC)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    const KiottyNetStatefulAppContext* stateful = kiottyTestCEchoServer();
    {
        RunningStatefulServer running(server, *stateful);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
        EXPECT_TRUE(echoOnce(port, "from a C table"));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    }
    EXPECT_EQ(1, kiottyTestCEchoReleased());
    KiottyNet_destroyServer(context, server);
}

TEST_F(StatefulTest, RunStatefulRejectsNullAndDuplicateRules)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    const KiottyNetStatefulAppContext stateful = serverStateful(ECHO_MACHINE);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runStatefulServer(nullptr, &stateful));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runStatefulServer(server, nullptr));
    const KiottyNetStatefulAppContext no_machine = { &app, nullptr, serverAcquire, serverRelease, serverOnError };
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runStatefulServer(server, &no_machine));
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_runStatefulServer(server, kiottyTestCDuplicateRules()));
    KiottyNetEventClientHandle client = createEventClient(port);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runStatefulEventClient(client, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_runStatefulEventClient(client, kiottyTestCDuplicateRules()));
    KiottyNet_destroyEventClient(context, client);
    {
        RunningStatefulServer running(server, stateful);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    }
    KiottyNet_destroyServer(context, server);
}

TEST_F(StatefulTest, RunStatefulClosesOnUnhandledTimeout)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    const KiottyNetStatefulAppContext stateful = serverStateful(NO_TIMEOUT_MACHINE);
    RunningStatefulServer running(server, stateful);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
    KiottyNetStreamClientHandle client = connectStreamClient(port);
    char buffer[4] = {};
    const KiottyNetResult read = KiottyNet_readStreamClient(client, buffer, sizeof(buffer), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, read.code);
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return app.released.load() == 1; }, WAIT_MS));
    EXPECT_TRUE(app.errors.has(KIOTTY_NET_CODE_UNHANDLED_EVENT));
    EXPECT_EQ(1, app.closed.load());
    KiottyNet_destroyStreamClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyServer(context, server);
}

TEST_F(StatefulTest, SessionNullIgnoresTheConnectionAndReportsOnError)
{
    app.exhausted = true;
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    const KiottyNetStatefulAppContext stateful = serverStateful(ECHO_MACHINE);
    RunningStatefulServer running(server, stateful);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
    KiottyNetStreamClientHandle client = connectStreamClient(port);
    char buffer[4] = {};
    const KiottyNetResult read = KiottyNet_readStreamClient(client, buffer, sizeof(buffer), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, read.code);
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return app.errors.has(KIOTTY_NET_CODE_SESSION_EXHAUSTED); }, WAIT_MS));
    EXPECT_EQ(1, app.created.load());
    EXPECT_EQ(0, app.closed.load());
    EXPECT_EQ(0, app.released.load());
    KiottyNet_destroyStreamClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyServer(context, server);
}

TEST_F(StatefulTest, StatefulWorkStartedRowHandsTheStreamToAWorker)
{
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetServerHandle server = createServer(port);
    ASSERT_NE(nullptr, server);
    const KiottyNetStatefulAppContext stateful = serverStateful(WORK_MACHINE);
    RunningStatefulServer running(server, stateful);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(server, WAIT_MS));
    KiottyNetStreamClientHandle client = connectStreamClient(port);
    ASSERT_TRUE(kiotty_test::waitUntil([&]() { return app.handed.load() != nullptr; }, WAIT_MS));
    const KiottyNetConnectionStream stream = *app.handed.load();

    const KiottyNetSpan ping = { "ping", 4 };
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeStreamClient(client, ping, WAIT_MS).code);
    char received[4] = {};
    const KiottyNetResult read = KiottyNet_readConnectionStream(stream, received, sizeof(received), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, read.code);
    EXPECT_EQ("ping", std::string(received, static_cast<size_t>(read.bytes)));
    const KiottyNetSpan pong = { "pong", 4 };
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_closeConnectionStream(stream, KiottyNet_thenWrite(pong, WAIT_MS)));

    char answered[4] = {};
    const KiottyNetResult reply = KiottyNet_readStreamClient(client, answered, sizeof(answered), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ("pong", std::string(answered, static_cast<size_t>(reply.bytes)));
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return app.released.load() == 1; }, WAIT_MS));
    EXPECT_EQ(1, app.closed.load());
    EXPECT_EQ(0u, app.errors.size());
    KiottyNet_destroyStreamClient(context, client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, running.stop());
    KiottyNet_destroyServer(context, server);
}
