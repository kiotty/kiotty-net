#include "kiotty_net_websocket.h"

#include "data/infra/tls/kiotty_tls_profile.h"
#include "domain/usecases/websocket/kiotty_websocket_handshake.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;

    struct Closed
    {
        KiottyNetCode code;
        uint16_t      status;
        std::string   reason;
    };

    struct Side
    {
        Side()
            : mutex()
            , messages()
            , closes()
            , errors()
            , opened(0)
            , is_echo(false)
        {
        }

        std::vector<std::string> messageList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return messages;
        }

        std::vector<Closed> closeList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return closes;
        }

        std::vector<KiottyNetCode> errorList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return errors;
        }

        std::mutex                 mutex;
        std::vector<std::string>   messages;
        std::vector<Closed>        closes;
        std::vector<KiottyNetCode> errors;
        std::atomic<int>         opened;
        bool                     is_echo;
    };

    Side& sideOf(void* context)
    {
        return *static_cast<Side*>(context);
    }

    std::string textOf(KiottyNetSpan span)
    {
        return span.data == nullptr ? std::string() : std::string(span.data, span.length);
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        const KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    KiottyNetWebSocketContext onOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle)
    {
        sideOf(app_context).opened.fetch_add(1);
        return app_context;
    }

    void onMessage(KiottyNetWebSocketContext websocket_context,
                   KiottyNetWebSocketHandle websocket,
                   KiottyNetWebSocketMessage message)
    {
        Side& side = sideOf(websocket_context);
        {
            const std::lock_guard<std::mutex> lock(side.mutex);
            side.messages.push_back(textOf(message.data));
        }
        if (side.is_echo) { (void)KiottyNet_sendWebSocket(websocket, message.opcode, message.data); }
    }

    void onClose(KiottyNetAppContext app_context,
                 KiottyNetWebSocketContext,
                 KiottyNetWebSocketClose close)
    {
        Side& side = sideOf(app_context);
        const std::lock_guard<std::mutex> lock(side.mutex);
        side.closes.push_back(Closed{ close.code, close.status, textOf(close.reason) });
    }

    void onClientOpen(KiottyNetAppContext app_context)
    {
        sideOf(app_context).opened.fetch_add(1);
    }

    void onClientMessage(KiottyNetAppContext app_context, KiottyNetWebSocketMessage message)
    {
        Side& side = sideOf(app_context);
        const std::lock_guard<std::mutex> lock(side.mutex);
        side.messages.push_back(textOf(message.data));
    }

    void onClientClose(KiottyNetAppContext app_context, KiottyNetWebSocketClose close)
    {
        Side& side = sideOf(app_context);
        const std::lock_guard<std::mutex> lock(side.mutex);
        side.closes.push_back(Closed{ close.code, close.status, textOf(close.reason) });
    }

    void onClientError(KiottyNetAppContext app_context, KiottyNetConnectionInfo, KiottyNetCode code, const char* message)
    {
        Side& side = sideOf(app_context);
        const std::lock_guard<std::mutex> lock(side.mutex);
        side.errors.push_back(message == nullptr ? KIOTTY_NET_CODE_INVALID_ARGUMENT : code);
    }

    std::string certPath(const char* name)
    {
        return std::string(KIOTTY_TEST_CERT_DIR) + "/" + name;
    }

    bool readHead(kiotty_test::LoopbackPeer& peer, std::string& head)
    {
        head.clear();
        while (head.size() < 4 || head.compare(head.size() - 4, 4, "\r\n\r\n") != 0)
        {
            char next = 0;
            if (!peer.recvExactly(&next, 1, WAIT_MS)) { return false; }
            head.push_back(next);
        }
        return true;
    }

    std::string fieldOf(const std::string& head, const std::string& name)
    {
        const std::string key = "\r\n" + name + ": ";
        const size_t start = head.find(key);
        if (start == std::string::npos) { return std::string(); }
        const size_t value = start + key.size();
        return head.substr(value, head.find("\r\n", value) - value);
    }

    std::string acceptFor(const std::string& key)
    {
        const kiotty::net::implementation::Span<const char> text{ key.data(), key.size() };
        const kiotty::net::implementation::AcceptText accept = kiotty::net::implementation::acceptOf(text);
        return std::string(accept.span().data, accept.span().length);
    }

    class WebSocketClientTest : public ::testing::Test
    {
    protected:
        WebSocketClientTest()
            : context(nullptr)
            , server(nullptr)
            , client(nullptr)
            , port(0)
            , server_side()
            , client_side()
            , limits()
            , client_configuration()
            , run_code(KIOTTY_NET_CODE_SUCCESS)
            , _server_address()
            , _client_address()
            , _client_path()
            , _server_thread()
            , _client_thread()
        {
        }

        void SetUp() override
        {
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&client_configuration));
            port = kiotty_test::unusedPort();
        }

        void TearDown() override
        {
            if (client != nullptr) { KiottyNet_closeWebSocketClient(client); }
            if (_client_thread.joinable()) { _client_thread.join(); }
            if (server != nullptr) { KiottyNet_closeHttpServer(server); }
            if (_server_thread.joinable()) { _server_thread.join(); }
            if (client != nullptr) { KiottyNet_destroyWebSocketClient(context, client); }
            if (server != nullptr) { KiottyNet_destroyHttpServer(context, server); }
            KiottyNet_releaseContext(context);
        }

        void startServer(const KiottyNetTlsServerConfiguration* tls)
        {
            KiottyNetServerConfiguration configuration;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&configuration));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS,
                      KiottyNet_configureServerBufferCapacity(&configuration, 4096, 4096, 16384));
            const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &configuration, &limits);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            server = created.handle;
            server_side.is_echo = true;
            const KiottyNetCode added = KiottyNet_WebSocketRoute(server, "/chat").onOpen(onOpen).onMessage(onMessage).onClose(onClose).build();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, added);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_server_address, LOOPBACK_HOST, port));
            const KiottyNetTlsServerConfiguration* copied = tls;
            _server_thread = std::thread([this, copied]() { (void)KiottyNet_runHttpServer(server, &_server_address, copied, &server_side); });
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitHttpServer(server, WAIT_MS));
        }

        void createClient()
        {
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS,
                      KiottyNet_configureClientBufferCapacity(&client_configuration, 4096, 4096, 16384));
            const KiottyNetWebSocketClientResult created =
                KiottyNet_createWebSocketClient(context, &client_configuration, &limits);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            client = created.handle;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onWebSocketClientOpen(client, onClientOpen));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onWebSocketClientMessage(client, onClientMessage));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onWebSocketClientClose(client, onClientClose));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onWebSocketClientError(client, onClientError));
        }

        void startClient(const KiottyNetTlsClientConfiguration* tls, const std::string& path = "/chat")
        {
            if (_client_thread.joinable()) { _client_thread.join(); }
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_client_address, LOOPBACK_HOST, port));
            run_code.store(KIOTTY_NET_CODE_SUCCESS);
            _client_path = path;
            _client_thread = std::thread([this, tls]() {
                run_code.store(KiottyNet_runWebSocketClient(client, &_client_address, _client_path.c_str(), tls, &client_side));
            });
        }

        void stopServer()
        {
            KiottyNet_closeHttpServer(server);
            if (_server_thread.joinable()) { _server_thread.join(); }
            KiottyNet_destroyHttpServer(context, server);
            server = nullptr;
        }

        KiottyNetCode finishClient()
        {
            if (_client_thread.joinable()) { _client_thread.join(); }
            return run_code.load();
        }

        bool waitClientMessages(size_t count)
        {
            return kiotty_test::waitUntil([this, count]() { return client_side.messageList().size() >= count; }, WAIT_MS);
        }

        void roundTrip(const std::string& text)
        {
            const size_t before = client_side.messageList().size();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_sendWebSocketClient(client, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf(text)));
            ASSERT_TRUE(waitClientMessages(before + 1));
            EXPECT_EQ(text, client_side.messageList().back());
        }

        void closeFromClient(uint16_t status)
        {
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_byeWebSocketClient(client, status, "bye"));
        }

        KiottyNetContext               context;
        KiottyNetHttpServerHandle      server;
        KiottyNetWebSocketClientHandle client;
        uint16_t                       port;
        Side                           server_side;
        Side                           client_side;
        KiottyNetHttpLimits            limits;
        KiottyNetClientConfiguration   client_configuration;
        std::atomic<KiottyNetCode>     run_code;

    private:
        KiottyNetAddress _server_address;
        KiottyNetAddress _client_address;
        std::string      _client_path;
        std::thread      _server_thread;
        std::thread      _client_thread;
    };

    class FakeServer
    {
    public:
        explicit FakeServer(uint16_t port)
            : _peer()
            , _is_listening(_peer.listenTcp(port))
        {
        }

        bool isListening() const
        {
            return _is_listening;
        }

        bool acceptUpgrade(std::string& head)
        {
            return _peer.accept(WAIT_MS) && readHead(_peer, head);
        }

        bool send(const std::string& text)
        {
            return _peer.sendAll(text.data(), text.size());
        }

        bool read(std::string& bytes, size_t length)
        {
            bytes.assign(length, '\0');
            return _peer.recvExactly(&bytes[0], length, WAIT_MS);
        }

    private:
        kiotty_test::LoopbackPeer _peer;
        bool                      _is_listening;
    };

    std::string switchingResponse(const std::string& accept, const std::string& extra)
    {
        return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Accept: " + accept + "\r\n" + extra + "\r\n";
    }
}

TEST_F(WebSocketClientTest, RoundTripsWithTheServerThenClosesFromTheClient)
{
    startServer(nullptr);
    createClient();
    EXPECT_EQ(nullptr, KiottyNet_getWebSocketClientSocket(client).owner);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED,
              KiottyNet_sendWebSocketClient(client, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf("early")));
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_byeWebSocketClient(client, 1000, nullptr));
    startClient(nullptr);
    const KiottyNetCode awaited = KiottyNet_awaitWebSocketClient(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, awaited) << "server opened " << server_side.opened.load()
                                                << " client opened " << client_side.opened.load();
    EXPECT_EQ(1, client_side.opened.load());
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_onWebSocketClientOpen(client, onClientOpen));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_setWebSocketClientProtocols(client, "chat"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_byeWebSocketClient(client, 1005, nullptr));
    const KiottyNetWebSocketInfo info = KiottyNet_getWebSocketInfo(KiottyNet_getWebSocketClientSocket(client));
    EXPECT_EQ(static_cast<int32_t>(port), info.connection.port);
    roundTrip("hello");
    roundTrip(std::string(12000, 'z'));
    closeFromClient(1000);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, finishClient());
    const std::vector<Closed> closes = client_side.closeList();
    ASSERT_EQ(1u, closes.size());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, closes[0].code);
    EXPECT_EQ(1000, closes[0].status);
    ASSERT_TRUE(kiotty_test::waitUntil([this]() { return !server_side.closeList().empty(); }, WAIT_MS));
    EXPECT_EQ(1000, server_side.closeList()[0].status);
    EXPECT_EQ("bye", server_side.closeList()[0].reason);
    EXPECT_EQ(2u, server_side.messageList().size());
    EXPECT_TRUE(client_side.errorList().empty());
}

TEST_F(WebSocketClientTest, OneClientRunsPlainThenSecure)
{
    if (!kiotty::net::implementation::tlsSupported()) { GTEST_SKIP() << "built without TLS"; }
    startServer(nullptr);
    createClient();
    startClient(nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitWebSocketClient(client, WAIT_MS));
    roundTrip("plain");
    KiottyNet_closeWebSocketClient(client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, finishClient());
    stopServer();
    KiottyNetTlsServerConfiguration server_tls;
    const std::string cert = certPath("server.pem");
    const std::string key = certPath("server.key");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&server_tls, cert.c_str(), key.c_str(), nullptr));
    port = kiotty_test::unusedPort();
    startServer(&server_tls);
    KiottyNetTlsClientConfiguration client_tls;
    const std::string trust = certPath("ca.pem");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsClientConfiguration(&client_tls));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsClientTrustStore(&client_tls, trust.c_str()));
    startClient(&client_tls);
    ASSERT_TRUE(kiotty_test::waitUntil([this]() { return client_side.opened.load() >= 2; }, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitWebSocketClient(client, WAIT_MS));
    roundTrip("secure");
    KiottyNet_closeWebSocketClient(client);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, finishClient());
    const std::vector<Closed> closes = client_side.closeList();
    ASSERT_EQ(2u, closes.size());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, closes[1].code);
    EXPECT_EQ(1001, closes[1].status);
}

TEST_F(WebSocketClientTest, SendsAWellFormedUpgradeAndRejectsWrongAnswers)
{
    createClient();
    const char* answers[] = { "status", "accept", "protocol" };
    for (const char* answer : answers)
    {
        port = kiotty_test::unusedPort();
        FakeServer fake(port);
        ASSERT_TRUE(fake.isListening());
        startClient(nullptr, "/chat?room=1");
        std::string head;
        ASSERT_TRUE(fake.acceptUpgrade(head));
        EXPECT_EQ(0u, head.find("GET /chat?room=1 HTTP/1.1\r\n"));
        EXPECT_EQ(std::string("127.0.0.1:") + std::to_string(port), fieldOf(head, "Host"));
        EXPECT_EQ("websocket", fieldOf(head, "Upgrade"));
        EXPECT_EQ("Upgrade", fieldOf(head, "Connection"));
        EXPECT_EQ("13", fieldOf(head, "Sec-WebSocket-Version"));
        EXPECT_EQ("", fieldOf(head, "Sec-WebSocket-Protocol"));
        const std::string key = fieldOf(head, "Sec-WebSocket-Key");
        EXPECT_EQ(24u, key.size());
        const std::string answer_text = answer;
        if (answer_text == "status") { fake.send("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"); }
        if (answer_text == "accept") { fake.send(switchingResponse("AAAAAAAAAAAAAAAAAAAAAAAAAAA=", "")); }
        if (answer_text == "protocol") { fake.send(switchingResponse(acceptFor(key), "Sec-WebSocket-Protocol: mqtt\r\n")); }
        EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, finishClient()) << answer;
        EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, KiottyNet_awaitWebSocketClient(client, WAIT_MS)) << answer;
    }
    EXPECT_EQ(0, client_side.opened.load());
    EXPECT_TRUE(client_side.closeList().empty());
    const std::vector<KiottyNetCode> expected_errors(3, KIOTTY_NET_CODE_PROTOCOL_ERROR);
    EXPECT_EQ(expected_errors, client_side.errorList());
}

TEST_F(WebSocketClientTest, OffersSubprotocolsAndAcceptsOneOfThemOrNone)
{
    createClient();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setWebSocketClientProtocols(client, "ocpp1.6, ocpp2.0.1"));
    const char* answers[] = { "ocpp2.0.1", "", "mqtt" };
    for (const char* answer : answers)
    {
        const std::string answer_text = answer;
        port = kiotty_test::unusedPort();
        FakeServer fake(port);
        ASSERT_TRUE(fake.isListening());
        startClient(nullptr);
        std::string head;
        ASSERT_TRUE(fake.acceptUpgrade(head));
        EXPECT_EQ("ocpp1.6, ocpp2.0.1", fieldOf(head, "Sec-WebSocket-Protocol"));
        const std::string extra = answer_text.empty() ? "" : "Sec-WebSocket-Protocol: " + answer_text + "\r\n";
        ASSERT_TRUE(fake.send(switchingResponse(acceptFor(fieldOf(head, "Sec-WebSocket-Key")), extra)));
        if (answer_text == "mqtt")
        {
            EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, finishClient());
            continue;
        }
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitWebSocketClient(client, WAIT_MS)) << answer;
        const KiottyNetWebSocketInfo info = KiottyNet_getWebSocketInfo(KiottyNet_getWebSocketClientSocket(client));
        EXPECT_EQ(answer_text, std::string(info.protocol));
        KiottyNet_closeWebSocketClient(client);
        EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, finishClient());
    }
    EXPECT_EQ(2, client_side.opened.load());
    const std::vector<Closed> closes = client_side.closeList();
    ASSERT_EQ(2u, closes.size());
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, closes[1].code);
    EXPECT_EQ(1001, closes[1].status);
    const std::vector<KiottyNetCode> expected_errors = { KIOTTY_NET_CODE_PROTOCOL_ERROR };
    EXPECT_EQ(expected_errors, client_side.errorList());
}

TEST_F(WebSocketClientTest, MasksOutgoingFramesAndRejectsAMaskedServerFrame)
{
    createClient();
    FakeServer fake(port);
    ASSERT_TRUE(fake.isListening());
    startClient(nullptr);
    std::string head;
    ASSERT_TRUE(fake.acceptUpgrade(head));
    ASSERT_TRUE(fake.send(switchingResponse(acceptFor(fieldOf(head, "Sec-WebSocket-Key")), "")));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitWebSocketClient(client, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_sendWebSocketClient(client, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf("abc")));
    std::string frame;
    ASSERT_TRUE(fake.read(frame, 9));
    EXPECT_EQ(static_cast<char>(0x81), frame[0]);
    EXPECT_EQ(static_cast<char>(0x83), frame[1]);
    std::string unmasked;
    for (size_t i = 0; i < 3; ++i) { unmasked.push_back(static_cast<char>(frame[6 + i] ^ frame[2 + i % 4])); }
    EXPECT_EQ("abc", unmasked);
    const char masked_frame[] = { static_cast<char>(0x81), static_cast<char>(0x81), 1, 2, 3, 4, 'x' };
    ASSERT_TRUE(fake.send(std::string(masked_frame, sizeof(masked_frame))));
    std::string close;
    ASSERT_TRUE(fake.read(close, 8));
    EXPECT_EQ(static_cast<char>(0x88), close[0]);
    EXPECT_EQ(static_cast<char>(0x82), close[1]);
    const uint16_t status = static_cast<uint16_t>(((close[6] ^ close[2]) & 0xFF) << 8 | ((close[7] ^ close[3]) & 0xFF));
    EXPECT_EQ(1002, status);
    EXPECT_NE(frame.substr(2, 4), close.substr(2, 4));
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, finishClient());
    const std::vector<Closed> closes = client_side.closeList();
    ASSERT_EQ(1u, closes.size());
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, closes[0].code);
    EXPECT_EQ(1002, closes[0].status);
    EXPECT_TRUE(client_side.errorList().empty());
}

TEST_F(WebSocketClientTest, SilentServerEndsTheHandshakeWithTimeout)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpTimeouts(&limits, 200, 15000));
    createClient();
    FakeServer fake(port);
    ASSERT_TRUE(fake.isListening());
    startClient(nullptr);
    std::string head;
    ASSERT_TRUE(fake.acceptUpgrade(head));
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, finishClient());
    const std::vector<KiottyNetCode> expected_errors = { KIOTTY_NET_CODE_TIMEOUT };
    EXPECT_EQ(expected_errors, client_side.errorList());
}

TEST_F(WebSocketClientTest, GivingUpOnConnectingIsReportedOnceThroughOnError)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientRetry(&client_configuration, 2, 50));
    createClient();
    startClient(nullptr);
    const KiottyNetCode code = finishClient();
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, code);
    EXPECT_NE(KIOTTY_NET_CODE_CLOSED, code);
    const std::vector<KiottyNetCode> expected_errors = { code };
    EXPECT_EQ(expected_errors, client_side.errorList());
    EXPECT_EQ(0, client_side.opened.load());
    EXPECT_TRUE(client_side.closeList().empty());
}

TEST_F(WebSocketClientTest, CppClientRegistersRunsSendsAndSaysBye)
{
    startServer(nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientBufferCapacity(&client_configuration, 4096, 4096, 16384));
    KiottyNet_WebSocketClient cpp(context, client_configuration, &limits);
    ASSERT_TRUE(static_cast<bool>(cpp));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.protocols(nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.onOpen(onClientOpen));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.onMessage(onClientMessage));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.onClose(onClientClose));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.onError(onClientError));
    KiottyNetAddress address;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port));
    std::atomic<KiottyNetCode> code(KIOTTY_NET_CODE_SUCCESS);
    std::thread io([&]() { code.store(cpp.run(address, "/chat", &client_side)); });
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.await(WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.send(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf("cpp")));
    ASSERT_TRUE(waitClientMessages(1));
    EXPECT_EQ("cpp", client_side.messageList().back());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.bye());
    io.join();
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, code.load());
    const std::vector<Closed> closes = client_side.closeList();
    ASSERT_EQ(1u, closes.size());
    EXPECT_EQ(1000, closes[0].status);
}

TEST_F(WebSocketClientTest, RetriesUntilTheServerListens)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientRetry(&client_configuration, 50, 100));
    createClient();
    startClient(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    startServer(nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitWebSocketClient(client, WAIT_MS));
    roundTrip("late");
}

TEST_F(WebSocketClientTest, RegistrationAndRunRejectBadArguments)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_createWebSocketClient(context, nullptr, nullptr).code);
    const KiottyNetWebSocketClientResult defaults = KiottyNet_createWebSocketClient(context, &client_configuration, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, defaults.code);
    client = defaults.handle;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_onWebSocketClientMessage(client, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_onWebSocketClientOpen(nullptr, onClientOpen));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_setWebSocketClientProtocols(client, "a,,b"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_setWebSocketClientProtocols(client, "bad token"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_setWebSocketClientProtocols(client, std::string(200, 'p').c_str()));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setWebSocketClientProtocols(client, "v1 , v2"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setWebSocketClientProtocols(client, nullptr));
    KiottyNetAddress address;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runWebSocketClient(client, &address, "/chat", nullptr, nullptr));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onWebSocketClientMessage(client, onClientMessage));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runWebSocketClient(client, &address, "chat", nullptr, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runWebSocketClient(client, &address, "/a b", nullptr, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runWebSocketClient(client, &address, nullptr, nullptr, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_runWebSocketClient(client, nullptr, "/chat", nullptr, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_byeWebSocketClient(nullptr, 1000, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, KiottyNet_awaitWebSocketClient(client, 10));
    EXPECT_TRUE(client_side.errorList().empty());
}

#endif
