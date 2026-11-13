#include "kiotty_net_websocket.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;
    const char*    SAMPLE_KEY = "dGhlIHNhbXBsZSBub25jZQ==";
    const char*    SAMPLE_ACCEPT = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

    const uint8_t CONTINUATION = 0x0;
    const uint8_t TEXT = 0x1;
    const uint8_t BINARY = 0x2;
    const uint8_t CLOSE = 0x8;
    const uint8_t PING = 0x9;
    const uint8_t PONG = 0xA;

    struct Frame
    {
        uint8_t     opcode;
        bool        fin;
        std::string payload;
    };

    struct Closed
    {
        KiottyNetCode code;
        uint16_t      status;
        std::string   reason;
    };

    struct Received
    {
        int         socket;
        int         opcode;
        std::string data;
    };

    struct SocketApp;

    struct PathContext
    {
        SocketApp* app;
        int        path;
    };

    struct SocketApp
    {
        SocketApp()
            : mutex()
            , sockets()
            , messages()
            , closes()
            , codes()
            , protocols()
            , tokens()
            , required_token()
            , wanted_protocol()
            , is_echo(true)
        {
        }

        void add(KiottyNetCode code)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            codes.push_back(code);
        }

        template <typename T>
        std::vector<T> copyOf(const std::vector<T>& list)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return list;
        }

        size_t socketCount()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return sockets.size();
        }

        KiottyNetWebSocketHandle socketAt(size_t index)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return sockets.at(index);
        }

        std::mutex                            mutex;
        std::vector<KiottyNetWebSocketHandle> sockets;
        std::vector<Received>                 messages;
        std::vector<Closed>                   closes;
        std::vector<KiottyNetCode>            codes;
        std::vector<std::string>              protocols;
        std::vector<std::string>              tokens;
        std::string                           required_token;
        std::string                           wanted_protocol;
        bool                                  is_echo;
    };

    PathContext& pathOf(KiottyNetAppContext context)
    {
        return *static_cast<PathContext*>(context);
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        const KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    std::string textOf(KiottyNetSpan span)
    {
        return span.data == nullptr ? std::string() : std::string(span.data, span.length);
    }

    KiottyNetBool onUpgrade(KiottyNetAppContext app_context,
                            KiottyNetWebSocketHandle websocket,
                            KiottyNetWebSocketUpgrade upgrade)
    {
        SocketApp& app = *pathOf(app_context).app;
        const std::string token = textOf(KiottyNet_findWebSocketUpgradeHeader(websocket, "X-Token"));
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.tokens.push_back(token);
        app.protocols.push_back(textOf(upgrade.protocols));
        if (!app.wanted_protocol.empty())
        {
            app.codes.push_back(KiottyNet_selectWebSocketProtocol(websocket, "not-offered"));
            app.codes.push_back(KiottyNet_selectWebSocketProtocol(websocket, app.wanted_protocol.c_str()));
        }
        const bool is_allowed = app.required_token.empty() || token == app.required_token;
        return is_allowed ? 1 : 0;
    }

    KiottyNetWebSocketContext onOpen(KiottyNetAppContext app_context, KiottyNetWebSocketHandle websocket)
    {
        PathContext& path = pathOf(app_context);
        SocketApp& app = *path.app;
        const KiottyNetWebSocketInfo info = KiottyNet_getWebSocketInfo(websocket);
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.sockets.push_back(websocket);
        app.protocols.push_back(std::string("open:") + info.protocol);
        app.codes.push_back(KiottyNet_selectWebSocketProtocol(websocket, "mqtt"));
        return &path;
    }

    void onMessage(KiottyNetWebSocketContext websocket_context,
                   KiottyNetWebSocketHandle websocket,
                   KiottyNetWebSocketMessage message)
    {
        PathContext& path = *static_cast<PathContext*>(websocket_context);
        SocketApp& app = *path.app;
        const std::string data = textOf(message.data);
        bool is_echo = false;
        {
            const std::lock_guard<std::mutex> lock(app.mutex);
            app.messages.push_back(Received{ path.path, static_cast<int>(message.opcode), data });
            is_echo = app.is_echo;
        }
        if (data == "close-now") { app.add(KiottyNet_closeWebSocket(websocket, 4001, "asked")); }
        else if (is_echo) { app.add(KiottyNet_sendWebSocket(websocket, message.opcode, message.data)); }
    }

    void onClose(KiottyNetAppContext app_context,
                 KiottyNetWebSocketContext websocket_context,
                 KiottyNetWebSocketHandle websocket,
                 KiottyNetWebSocketClose close)
    {
        SocketApp& app = *pathOf(app_context).app;
        const KiottyNetSpan data = { "late", 4 };
        const KiottyNetCode late = KiottyNet_sendWebSocket(websocket, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, data);
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.closes.push_back(Closed{ close.code, close.status, textOf(close.reason) });
        app.codes.push_back(websocket_context == app_context ? late : KIOTTY_NET_CODE_INVALID_ARGUMENT);
    }

    const KiottyNetWebSocketHandler HANDLER = { onUpgrade, onOpen, onMessage, onClose };

    std::string frameOf(uint8_t opcode, const std::string& payload, bool fin, bool masked)
    {
        std::string frame;
        frame.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
        const uint8_t mask_bit = masked ? 0x80 : 0x00;
        const size_t length = payload.size();
        if (length < 126) { frame.push_back(static_cast<char>(mask_bit | length)); }
        else if (length <= 0xFFFF)
        {
            frame.push_back(static_cast<char>(mask_bit | 126));
            frame.push_back(static_cast<char>((length >> 8) & 0xFF));
            frame.push_back(static_cast<char>(length & 0xFF));
        }
        else
        {
            frame.push_back(static_cast<char>(mask_bit | 127));
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                frame.push_back(static_cast<char>((static_cast<uint64_t>(length) >> shift) & 0xFF));
            }
        }
        const char key[4] = { 0x37, static_cast<char>(0xFA), 0x21, 0x3D };
        if (masked) { frame.append(key, 4); }
        for (size_t i = 0; i < length; ++i)
        {
            frame.push_back(masked ? static_cast<char>(payload[i] ^ key[i % 4]) : payload[i]);
        }
        return frame;
    }

    std::string clientFrame(uint8_t opcode, const std::string& payload, bool fin = true)
    {
        return frameOf(opcode, payload, fin, true);
    }

    std::string closePayload(uint16_t status, const std::string& reason)
    {
        std::string payload;
        payload.push_back(static_cast<char>(status >> 8));
        payload.push_back(static_cast<char>(status & 0xFF));
        return payload + reason;
    }

    bool readFrame(kiotty_test::LoopbackPeer& peer, Frame& frame)
    {
        unsigned char start[2] = {};
        if (!peer.recvExactly(start, 2, WAIT_MS)) { return false; }
        frame.fin = (start[0] & 0x80) != 0;
        frame.opcode = static_cast<uint8_t>(start[0] & 0x0F);
        if ((start[1] & 0x80) != 0) { return false; }
        uint64_t length = start[1] & 0x7F;
        const size_t extended = length == 126 ? 2 : (length == 127 ? 8 : 0);
        if (extended > 0)
        {
            unsigned char bytes[8] = {};
            if (!peer.recvExactly(bytes, extended, WAIT_MS)) { return false; }
            length = 0;
            for (size_t i = 0; i < extended; ++i) { length = (length << 8) | bytes[i]; }
        }
        frame.payload.assign(static_cast<size_t>(length), '\0');
        return length == 0 || peer.recvExactly(&frame.payload[0], static_cast<size_t>(length), WAIT_MS);
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

    int statusOf(const std::string& head)
    {
        return head.size() < 12 ? -1 : std::atoi(head.substr(9, 3).c_str());
    }

    bool isClosedByServer(kiotty_test::LoopbackPeer& peer)
    {
        char next = 0;
        return !peer.recvExactly(&next, 1, WAIT_MS);
    }

    std::string upgradeRequest(const std::string& path, const std::string& extra)
    {
        return "GET " + path + " HTTP/1.1\r\nHost: device.local\r\nUpgrade: websocket\r\n"
               "Connection: keep-alive, Upgrade\r\nSec-WebSocket-Key: " + SAMPLE_KEY + "\r\n"
               "Sec-WebSocket-Version: 13\r\n" + extra + "\r\n";
    }

    void onHealth(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        const std::string body = "ok";
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, spanOf(body));
    }

    class WebSocketServerTest : public ::testing::Test
    {
    protected:
        WebSocketServerTest()
            : context(nullptr)
            , server(nullptr)
            , port(0)
            , app()
            , chat{ &app, 1 }
            , telemetry{ &app, 2 }
            , limits()
            , configuration()
            , _address()
            , _code(KIOTTY_NET_CODE_SUCCESS)
            , _thread()
        {
        }

        void SetUp() override
        {
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&configuration));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&configuration, 4));
        }

        void TearDown() override
        {
            stop();
            if (server != nullptr) { KiottyNet_destroyHttpServer(context, server); }
            KiottyNet_releaseContext(context);
        }

        void create()
        {
            const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &configuration, &limits);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            server = created.handle;
            const KiottyNetHttpRoute health = KiottyNet_makeHttpRoute(onHealth, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/health", health));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addWebSocket(server, "/chat", HANDLER, &chat));
        }

        void start()
        {
            if (server == nullptr) { create(); }
            port = kiotty_test::unusedPort();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, port));
            _thread = std::thread([this]() { _code.store(KiottyNet_runHttpServer(server, &_address, nullptr, &app)); });
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitHttpServer(server, WAIT_MS));
        }

        KiottyNetCode stop()
        {
            if (server != nullptr) { KiottyNet_closeHttpServer(server); }
            if (_thread.joinable()) { _thread.join(); }
            return _code.load();
        }

        void send(kiotty_test::LoopbackPeer& peer, const std::string& text)
        {
            ASSERT_TRUE(peer.sendAll(text.data(), text.size()));
        }

        void open(kiotty_test::LoopbackPeer& peer, const std::string& path, const std::string& extra = "")
        {
            const size_t before = app.socketCount();
            ASSERT_TRUE(peer.connectTcp(port));
            send(peer, upgradeRequest(path, extra));
            std::string head;
            ASSERT_TRUE(readHead(peer, head));
            ASSERT_EQ(101, statusOf(head));
            EXPECT_EQ(SAMPLE_ACCEPT, fieldOf(head, "Sec-WebSocket-Accept"));
            EXPECT_EQ("websocket", fieldOf(head, "Upgrade"));
            EXPECT_EQ("Upgrade", fieldOf(head, "Connection"));
            EXPECT_EQ("", fieldOf(head, "Content-Length"));
            ASSERT_TRUE(waitSockets(before + 1));
        }

        int refusedStatus(const std::string& request, std::string& head)
        {
            kiotty_test::LoopbackPeer peer;
            if (!peer.connectTcp(port)) { return -1; }
            if (!peer.sendAll(request.data(), request.size())) { return -1; }
            if (!readHead(peer, head)) { return -1; }
            return statusOf(head);
        }

        void expectFrame(kiotty_test::LoopbackPeer& peer, uint8_t opcode, const std::string& payload)
        {
            Frame frame = {};
            ASSERT_TRUE(readFrame(peer, frame)) << "expected opcode " << static_cast<int>(opcode);
            EXPECT_TRUE(frame.fin);
            EXPECT_EQ(opcode, frame.opcode);
            EXPECT_EQ(payload, frame.payload);
        }

        bool waitSockets(size_t count)
        {
            return kiotty_test::waitUntil([this, count]() { return app.socketCount() >= count; }, WAIT_MS);
        }

        bool waitCloses(size_t count)
        {
            return kiotty_test::waitUntil([this, count]() { return app.copyOf(app.closes).size() >= count; }, WAIT_MS);
        }

        bool waitMessages(size_t count)
        {
            return kiotty_test::waitUntil([this, count]() { return app.copyOf(app.messages).size() >= count; }, WAIT_MS);
        }

        KiottyNetContext             context;
        KiottyNetHttpServerHandle    server;
        uint16_t                     port;
        SocketApp                    app;
        PathContext                  chat;
        PathContext                  telemetry;
        KiottyNetHttpLimits          limits;
        KiottyNetServerConfiguration configuration;

    private:
        KiottyNetAddress           _address;
        std::atomic<KiottyNetCode> _code;
        std::thread                _thread;
    };
}

TEST_F(WebSocketServerTest, EchoesTextBinaryAndFragmentsThenAnswersTheClientClose)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerBufferCapacity(&configuration, 4096, 4096, 16384));
    start();
    kiotty_test::LoopbackPeer peer;
    open(peer, "/chat");
    send(peer, clientFrame(TEXT, "hello"));
    expectFrame(peer, TEXT, "hello");
    const std::string binary("\x00\x01\xFE\xFF", 4);
    send(peer, clientFrame(BINARY, binary));
    expectFrame(peer, BINARY, binary);
    send(peer, clientFrame(TEXT, "frag", false));
    send(peer, clientFrame(PING, "are you there"));
    send(peer, clientFrame(CONTINUATION, "ment", true));
    expectFrame(peer, PONG, "are you there");
    expectFrame(peer, TEXT, "fragment");
    const std::string large(12000, 'x');
    send(peer, clientFrame(BINARY, large));
    expectFrame(peer, BINARY, large);
    send(peer, clientFrame(CLOSE, closePayload(1000, "done")));
    expectFrame(peer, CLOSE, closePayload(1000, ""));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitCloses(1));
    const Closed closed = app.copyOf(app.closes).at(0);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, closed.code);
    EXPECT_EQ(1000, closed.status);
    EXPECT_EQ("done", closed.reason);
    const std::vector<Received> messages = app.copyOf(app.messages);
    ASSERT_EQ(4u, messages.size());
    EXPECT_EQ(KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, messages[2].opcode);
    EXPECT_EQ("fragment", messages[2].data);
}

TEST_F(WebSocketServerTest, MessageLargerThanTheBodyCapacityClosesWith1009)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpBodyCapacity(&limits, 1024));
    start();
    kiotty_test::LoopbackPeer peer;
    open(peer, "/chat");
    send(peer, clientFrame(TEXT, std::string(600, 'a'), false));
    send(peer, clientFrame(CONTINUATION, std::string(600, 'b'), true));
    expectFrame(peer, CLOSE, closePayload(1009, ""));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitCloses(1));
    const Closed closed = app.copyOf(app.closes).at(0);
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, closed.code);
    EXPECT_EQ(1009, closed.status);
    EXPECT_TRUE(app.copyOf(app.messages).empty());
}

TEST_F(WebSocketServerTest, BrokenFramesCloseWithProtocolErrorOrInvalidPayload)
{
    start();
    kiotty_test::LoopbackPeer unmasked;
    open(unmasked, "/chat");
    send(unmasked, frameOf(TEXT, "plain", true, false));
    expectFrame(unmasked, CLOSE, closePayload(1002, ""));
    EXPECT_TRUE(isClosedByServer(unmasked));
    kiotty_test::LoopbackPeer broken;
    open(broken, "/chat");
    send(broken, clientFrame(TEXT, std::string("\xC3\x28", 2)));
    expectFrame(broken, CLOSE, closePayload(1007, ""));
    EXPECT_TRUE(isClosedByServer(broken));
    kiotty_test::LoopbackPeer orphan;
    open(orphan, "/chat");
    send(orphan, clientFrame(CONTINUATION, "lost"));
    expectFrame(orphan, CLOSE, closePayload(1002, ""));
    ASSERT_TRUE(waitCloses(3));
    const std::vector<Closed> closes = app.copyOf(app.closes);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, closes[0].code);
    EXPECT_EQ(1002, closes[0].status);
    EXPECT_EQ(1007, closes[1].status);
    EXPECT_EQ(1002, closes[2].status);
}

TEST_F(WebSocketServerTest, PingTimerSendsPingsAndClosesWithoutAPong)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpWebSocketTimers(&limits, 100, 300, 1000));
    start();
    kiotty_test::LoopbackPeer peer;
    open(peer, "/chat");
    expectFrame(peer, PING, "");
    send(peer, clientFrame(PONG, ""));
    expectFrame(peer, PING, "");
    expectFrame(peer, CLOSE, closePayload(1001, ""));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitCloses(1));
    const Closed closed = app.copyOf(app.closes).at(0);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, closed.code);
    EXPECT_EQ(1001, closed.status);
}

TEST_F(WebSocketServerTest, CloseFromAnotherThreadEndsWhenThePeerAnswers)
{
    start();
    kiotty_test::LoopbackPeer peer;
    open(peer, "/chat");
    const KiottyNetWebSocketHandle socket = app.socketAt(0);
    KiottyNetCode codes[4] = {};
    std::thread closer([&codes, socket]() {
        const KiottyNetSpan data = { "x", 1 };
        codes[0] = KiottyNet_closeWebSocket(socket, 4000, "bye");
        codes[1] = KiottyNet_closeWebSocket(socket, 4000, "again");
        codes[2] = KiottyNet_sendWebSocket(socket, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, data);
        codes[3] = KiottyNet_closeWebSocket(socket, 1005, nullptr);
    });
    closer.join();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codes[0]);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, codes[1]);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, codes[2]);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, codes[3]);
    expectFrame(peer, CLOSE, closePayload(4000, "bye"));
    send(peer, clientFrame(TEXT, "ignored"));
    send(peer, clientFrame(CLOSE, closePayload(4000, "")));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitCloses(1));
    const Closed closed = app.copyOf(app.closes).at(0);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, closed.code);
    EXPECT_EQ(4000, closed.status);
    EXPECT_TRUE(app.copyOf(app.messages).empty());
    const KiottyNetSpan data = { "x", 1 };
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_sendWebSocket(socket, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, data));
}

TEST_F(WebSocketServerTest, CloseInsideOnMessageWaitsForThePeerCloseUntilTheTimeout)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpWebSocketTimers(&limits, 0, 0, 200));
    start();
    kiotty_test::LoopbackPeer peer;
    open(peer, "/chat");
    send(peer, clientFrame(TEXT, "close-now"));
    expectFrame(peer, CLOSE, closePayload(4001, "asked"));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitCloses(1));
    const Closed closed = app.copyOf(app.closes).at(0);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, closed.code);
    EXPECT_EQ(4001, closed.status);
}

TEST_F(WebSocketServerTest, RefusesWrongMethodMissingHeadersBadKeyAndForbiddenUpgrade)
{
    app.required_token = "secret";
    start();
    std::string head;
    EXPECT_EQ(405, refusedStatus("POST /chat HTTP/1.1\r\nContent-Length: 0\r\n\r\n", head));
    EXPECT_EQ(0u, fieldOf(head, "Allow").find("GET"));
    EXPECT_EQ(405, refusedStatus("HEAD /chat HTTP/1.1\r\n\r\n", head));
    EXPECT_EQ(426, refusedStatus("GET /chat HTTP/1.1\r\n\r\n", head));
    EXPECT_EQ("websocket", fieldOf(head, "Upgrade"));
    EXPECT_EQ("13", fieldOf(head, "Sec-WebSocket-Version"));
    std::string old_version = upgradeRequest("/chat", "X-Token: secret\r\n");
    old_version.replace(old_version.find("Version: 13"), 11, "Version: 8");
    EXPECT_EQ(426, refusedStatus(old_version, head));
    std::string bad_key = upgradeRequest("/chat", "X-Token: secret\r\n");
    bad_key.replace(bad_key.find(SAMPLE_KEY), 4, "!!!!");
    EXPECT_EQ(400, refusedStatus(bad_key, head));
    std::string with_body = upgradeRequest("/chat", "Content-Length: 2\r\n");
    EXPECT_EQ(400, refusedStatus(with_body + "{}", head));
    EXPECT_EQ(403, refusedStatus(upgradeRequest("/chat", "X-Token: wrong\r\n"), head));
    EXPECT_EQ("close", fieldOf(head, "Connection"));
    kiotty_test::LoopbackPeer allowed;
    open(allowed, "/chat", "X-Token: secret\r\n");
    const std::vector<std::string> tokens = app.copyOf(app.tokens);
    ASSERT_EQ(2u, tokens.size());
    EXPECT_EQ("wrong", tokens[0]);
    EXPECT_EQ("secret", tokens[1]);
}

TEST_F(WebSocketServerTest, SubprotocolIsChosenOnlyInsideOnUpgrade)
{
    app.wanted_protocol = "mqtt";
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    send(peer, upgradeRequest("/chat", "Sec-WebSocket-Protocol: v12.stomp, mqtt\r\n"));
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    ASSERT_EQ(101, statusOf(head));
    EXPECT_EQ("mqtt", fieldOf(head, "Sec-WebSocket-Protocol"));
    ASSERT_TRUE(waitSockets(1));
    const std::vector<std::string> protocols = app.copyOf(app.protocols);
    const std::vector<std::string> expected = { "v12.stomp, mqtt", "open:mqtt" };
    EXPECT_EQ(expected, protocols);
    const std::vector<KiottyNetCode> codes = app.copyOf(app.codes);
    const std::vector<KiottyNetCode> expected_codes = {
        KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_BUSY,
    };
    EXPECT_EQ(expected_codes, codes);
}

TEST_F(WebSocketServerTest, HttpRoutesAndTwoSocketPathsShareOneServerAndBroadcastByPath)
{
    create();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addWebSocket(server, "/telemetry", HANDLER, &telemetry));
    app.is_echo = false;
    start();
    kiotty_test::LoopbackPeer web;
    ASSERT_TRUE(web.connectTcp(port));
    send(web, "GET /health HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(web, head));
    EXPECT_EQ(200, statusOf(head));
    kiotty_test::LoopbackPeer chat_peer;
    open(chat_peer, "/chat");
    kiotty_test::LoopbackPeer telemetry_peer;
    open(telemetry_peer, "/telemetry");
    const std::string reading = "{\"t\":21}";
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS,
              KiottyNet_broadcastWebSocket(server, "/telemetry", KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf(reading)));
    expectFrame(telemetry_peer, TEXT, reading);
    const std::string all = "all";
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS,
              KiottyNet_broadcastWebSocket(server, nullptr, KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY, spanOf(all)));
    expectFrame(chat_peer, BINARY, all);
    expectFrame(telemetry_peer, BINARY, all);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT,
              KiottyNet_broadcastWebSocket(server, "/nope", KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf(all)));
    send(telemetry_peer, clientFrame(TEXT, "from telemetry"));
    send(chat_peer, clientFrame(TEXT, "from chat"));
    ASSERT_TRUE(waitMessages(2));
    for (const Received& received : app.copyOf(app.messages))
    {
        EXPECT_EQ(received.socket == 1 ? "from chat" : "from telemetry", received.data);
    }
}

TEST_F(WebSocketServerTest, RegistrationRulesAndServerCloseGivesClosed1001)
{
    create();
    const KiottyNetWebSocketHandler no_message = { nullptr, onOpen, nullptr, nullptr };
    const KiottyNetHttpRoute health = KiottyNet_makeHttpRoute(onHealth, nullptr);
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_addWebSocket(server, "/chat", HANDLER, &chat));
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_POST, "/chat", health));
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_addWebSocket(server, "/health", HANDLER, &chat));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_addWebSocket(server, "/other", no_message, &chat));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_addWebSocket(server, "chat", HANDLER, &chat));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_addWebSocket(nullptr, "/other", HANDLER, &chat));
    start();
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_addWebSocket(server, "/late", HANDLER, &chat));
    kiotty_test::LoopbackPeer peer;
    open(peer, "/chat");
    const KiottyNetWebSocketHandle socket = app.socketAt(0);
    const KiottyNetWebSocketInfo outside = KiottyNet_getWebSocketInfo(KiottyNetWebSocketHandle{ nullptr, 0 });
    EXPECT_EQ(0, outside.protocol[0]);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED,
              KiottyNet_sendWebSocket(KiottyNetWebSocketHandle{ nullptr, 0 }, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, spanOf("x")));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT,
              KiottyNet_sendWebSocket(socket, KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT, KiottyNetSpan{ nullptr, 0 }));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, stop());
    ASSERT_TRUE(waitCloses(1));
    const Closed closed = app.copyOf(app.closes).at(0);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, closed.code);
    EXPECT_EQ(1001, closed.status);
    const std::vector<KiottyNetCode> codes = app.copyOf(app.codes);
    ASSERT_FALSE(codes.empty());
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, codes.back());
}

#endif
