#include "kiotty_net_http.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;

    struct WorkApp;

    using Job = std::function<void(WorkApp&, KiottyNetHttpResponseHandle, KiottyNetHttpWorkHandle)>;

    struct WorkApp
    {
        WorkApp()
            : mutex()
            , workers()
            , codes()
            , job()
            , started(0)
            , large_bytes(0)
        {
        }

        void add(KiottyNetCode code)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            codes.push_back(code);
        }

        std::vector<KiottyNetCode> codeList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return codes;
        }

        void join()
        {
            std::vector<std::thread> taken;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                taken.swap(workers);
            }
            for (std::thread& worker : taken) { worker.join(); }
        }

        std::mutex                 mutex;
        std::vector<std::thread>   workers;
        std::vector<KiottyNetCode> codes;
        Job                        job;
        std::atomic<int>           started;
        size_t                     large_bytes;
    };

    WorkApp& appOf(KiottyNetAppContext context)
    {
        return *static_cast<WorkApp*>(context);
    }

    KiottyNetSpan spanOf(const char* text)
    {
        KiottyNetSpan span = { text, std::strlen(text) };
        return span;
    }

    std::string largeText(size_t length)
    {
        std::string text(length, 'a');
        for (size_t i = 0; i < length; ++i) { text[i] = static_cast<char>('a' + i % 26); }
        return text;
    }

    void onHealth(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf("{\"ok\":true}"));
    }

    void onStartWork(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        appOf(app_context).add(KiottyNet_startHttpWork(response));
    }

    void onStartWorkTwice(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        appOf(app_context).add(KiottyNet_startHttpWork(response));
        appOf(app_context).add(KiottyNet_startHttpWork(response));
    }

    void onStreamRoute(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_OK);
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        appOf(app_context).add(KiottyNet_startHttpWork(response));
    }

    void onOrphanStream(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
    }

    void onWorkStarted(KiottyNetAppContext app_context,
                       const KiottyNetHttpRequest*,
                       KiottyNetHttpResponseHandle response,
                       KiottyNetHttpWorkHandle work)
    {
        WorkApp& app = appOf(app_context);
        app.started.fetch_add(1);
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.workers.emplace_back([&app, response, work]() { app.job(app, response, work); });
    }

    void answerJson(WorkApp& app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_CREATED);
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf("{\"done\":1}"));
        app.add(KiottyNet_finishHttpWorkWithResponse(work));
    }

    void streamEvents(WorkApp& app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        app.add(KiottyNet_setHttpHeader(response, "Cache-Control", "no-cache"));
        app.add(KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM));
        const char* events[] = { "data: one\n\n", "data: two\n\n", "data: three\n\n" };
        for (const char* event : events) { app.add(KiottyNet_writeHttpWorkChunk(work, spanOf(event), WAIT_MS).code); }
        app.add(KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_ACCEPTED));
        app.add(KiottyNet_finishHttpWorkWithResponse(work));
    }

    void streamLarge(WorkApp& app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM);
        const std::string text = largeText(app.large_bytes);
        const KiottyNetSpan data = { text.data(), text.size() };
        const KiottyNetResult written = KiottyNet_writeHttpWorkChunk(work, data, WAIT_MS);
        app.add(written.code);
        app.add(written.bytes == static_cast<int32_t>(text.size()) ? KIOTTY_NET_CODE_SUCCESS : KIOTTY_NET_CODE_BUFFER_FULL);
        app.add(KiottyNet_finishHttpWorkWithResponse(work));
    }

    void streamOnHead(WorkApp& app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN);
        app.add(KiottyNet_writeHttpWorkChunk(work, spanOf("x"), WAIT_MS).code);
        app.add(KiottyNet_writeHttpWorkChunk(work, spanOf("y"), WAIT_MS).code);
        app.add(KiottyNet_finishHttpWorkWithResponse(work));
    }

    void pingUntilFailure(WorkApp& app, KiottyNetHttpResponseHandle, KiottyNetHttpWorkHandle work)
    {
        KiottyNetCode code = KIOTTY_NET_CODE_SUCCESS;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(WAIT_MS);
        while (code == KIOTTY_NET_CODE_SUCCESS && std::chrono::steady_clock::now() < until)
        {
            code = KiottyNet_writeHttpWorkChunk(work, spanOf(": ping\n\n"), WAIT_MS).code;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        app.add(code);
        app.add(KiottyNet_isHttpWorkConnected(work) == 0 ? KIOTTY_NET_CODE_SUCCESS : KIOTTY_NET_CODE_BUSY);
        KiottyNet_finishHttpWorkWithClose(work, code);
    }

    void checkRules(WorkApp& app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        const char broken[] = { '\xC3', '\x28', '\0' };
        const KiottyNetSpan empty = { "", 0 };
        app.add(KiottyNet_isHttpWorkConnected(work) == 1 ? KIOTTY_NET_CODE_SUCCESS : KIOTTY_NET_CODE_PEER_CLOSED);
        app.add(KiottyNet_writeHttpWorkChunk(work, spanOf("x"), WAIT_MS).code);
        app.add(KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION));
        app.add(KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN));
        app.add(KiottyNet_writeHttpWorkChunk(work, empty, WAIT_MS).code);
        app.add(KiottyNet_writeHttpWorkChunk(work, spanOf(broken), WAIT_MS).code);
        app.add(KiottyNet_startHttpWork(response));
        app.add(KiottyNet_writeHttpWorkChunk(work, spanOf("ok"), WAIT_MS).code);
        app.add(KiottyNet_finishHttpWorkWithResponse(work));
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

    bool readLine(kiotty_test::LoopbackPeer& peer, std::string& line)
    {
        line.clear();
        while (line.size() < 2 || line.compare(line.size() - 2, 2, "\r\n") != 0)
        {
            char next = 0;
            if (!peer.recvExactly(&next, 1, WAIT_MS)) { return false; }
            line.push_back(next);
        }
        line.resize(line.size() - 2);
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

    std::vector<std::string> readChunks(kiotty_test::LoopbackPeer& peer)
    {
        std::vector<std::string> chunks;
        std::string line;
        while (readLine(peer, line))
        {
            const size_t length = std::strtoul(line.c_str(), nullptr, 16);
            if (length == 0) { break; }
            std::string chunk(length, '\0');
            if (!peer.recvExactly(&chunk[0], length, WAIT_MS)) { break; }
            chunks.push_back(chunk);
            if (!readLine(peer, line) || !line.empty()) { break; }
        }
        if (readLine(peer, line) && line.empty()) { chunks.push_back("<end>"); }
        return chunks;
    }

    std::string readLengthBody(kiotty_test::LoopbackPeer& peer, const std::string& head)
    {
        const size_t length = std::strtoul(fieldOf(head, "Content-Length").c_str(), nullptr, 10);
        std::string body(length, '\0');
        if (length > 0 && !peer.recvExactly(&body[0], length, WAIT_MS)) { return std::string(); }
        return body;
    }

    bool isClosedByServer(kiotty_test::LoopbackPeer& peer)
    {
        char next = 0;
        return !peer.recvExactly(&next, 1, WAIT_MS);
    }

    class HttpWorkTest : public ::testing::Test
    {
    protected:
        HttpWorkTest()
            : context(nullptr)
            , server(nullptr)
            , port(0)
            , app()
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
            const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &configuration, &limits);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            server = created.handle;
            route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth, nullptr);
        }

        void TearDown() override
        {
            stop();
            app.join();
            if (server != nullptr) { KiottyNet_destroyHttpServer(context, server); }
            KiottyNet_releaseContext(context);
        }

        void route(KiottyNetHttpMethod method,
                   const char* path,
                   KiottyNetHttpRequestAction action,
                   KiottyNetHttpWorkAction work)
        {
            const KiottyNetHttpRoute made = KiottyNet_makeHttpRoute(&app, action, work);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, method, path, made));
        }

        void start()
        {
            port = kiotty_test::unusedPort();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, port));
            _thread = std::thread([this]() { _code.store(KiottyNet_runHttpServer(server, &_address, nullptr)); });
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitHttpServer(server, WAIT_MS));
        }

        KiottyNetCode stop()
        {
            if (server != nullptr) { KiottyNet_closeHttpServer(server); }
            if (_thread.joinable()) { _thread.join(); }
            return _code.load();
        }

        void connect(kiotty_test::LoopbackPeer& peer)
        {
            ASSERT_TRUE(peer.connectTcp(port));
        }

        void send(kiotty_test::LoopbackPeer& peer, const std::string& text)
        {
            ASSERT_TRUE(peer.sendAll(text.data(), text.size()));
        }

        void expectHealthy(kiotty_test::LoopbackPeer& peer)
        {
            send(peer, "GET /health HTTP/1.1\r\n\r\n");
            std::string head;
            ASSERT_TRUE(readHead(peer, head));
            EXPECT_EQ(200, statusOf(head));
            EXPECT_EQ("{\"ok\":true}", readLengthBody(peer, head));
        }

        bool waitCodes(size_t count)
        {
            return kiotty_test::waitUntil([this, count]() { return app.codeList().size() >= count; }, WAIT_MS);
        }

        KiottyNetContext             context;
        KiottyNetHttpServerHandle    server;
        uint16_t                     port;
        WorkApp                      app;
        KiottyNetHttpLimits          limits;
        KiottyNetServerConfiguration configuration;

    private:
        KiottyNetAddress           _address;
        std::atomic<KiottyNetCode> _code;
        std::thread                _thread;
    };
}

TEST_F(HttpWorkTest, WorkFinishesWithACompleteResponseAndKeepsAlive)
{
    app.job = answerJson;
    route(KIOTTY_NET_HTTP_METHOD_POST, "/work", onStartWork, onWorkStarted);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "POST /work HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ(201, statusOf(head));
    EXPECT_EQ("application/json", fieldOf(head, "Content-Type"));
    EXPECT_EQ("{\"done\":1}", readLengthBody(peer, head));
    ASSERT_TRUE(waitCodes(2));
    expectHealthy(peer);
    const std::vector<KiottyNetCode> expected = { KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, StreamSendsEachChunkThenTheLastChunkAndKeepsAlive)
{
    app.job = streamEvents;
    route(KIOTTY_NET_HTTP_METHOD_GET, "/events", onStartWork, onWorkStarted);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /events HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ(200, statusOf(head));
    EXPECT_EQ("chunked", fieldOf(head, "Transfer-Encoding"));
    EXPECT_EQ("", fieldOf(head, "Content-Length"));
    EXPECT_EQ("text/event-stream; charset=utf-8", fieldOf(head, "Content-Type"));
    EXPECT_EQ("no-cache", fieldOf(head, "Cache-Control"));
    const std::vector<std::string> chunks = { "data: one\n\n", "data: two\n\n", "data: three\n\n", "<end>" };
    EXPECT_EQ(chunks, readChunks(peer));
    ASSERT_TRUE(waitCodes(8));
    expectHealthy(peer);
    const std::vector<KiottyNetCode> expected = {
        KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS,
        KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_BUSY,    KIOTTY_NET_CODE_SUCCESS,
    };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, ChunkLargerThanTheSendCapacityGoesOutWhole)
{
    app.job = streamLarge;
    app.large_bytes = configuration.send_capacity * 3 + 17;
    route(KIOTTY_NET_HTTP_METHOD_GET, "/large", onStartWork, onWorkStarted);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /large HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ("chunked", fieldOf(head, "Transfer-Encoding"));
    const std::vector<std::string> chunks = { largeText(app.large_bytes), "<end>" };
    EXPECT_EQ(chunks, readChunks(peer));
    ASSERT_TRUE(waitCodes(4));
    expectHealthy(peer);
    const std::vector<KiottyNetCode> expected = { KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS,
                                                  KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_SUCCESS };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, HeadOnAStreamRouteSendsHeadersWithoutOpeningWork)
{
    app.job = streamEvents;
    route(KIOTTY_NET_HTTP_METHOD_GET, "/events", onStreamRoute, onWorkStarted);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "HEAD /events HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ(200, statusOf(head));
    EXPECT_EQ("chunked", fieldOf(head, "Transfer-Encoding"));
    EXPECT_EQ("text/event-stream; charset=utf-8", fieldOf(head, "Content-Type"));
    expectHealthy(peer);
    EXPECT_EQ(0, app.started.load());
    const std::vector<KiottyNetCode> expected = { KIOTTY_NET_CODE_SUCCESS };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, HeadOnAStreamChosenByTheWorkerClosesTheFirstWrite)
{
    app.job = streamOnHead;
    route(KIOTTY_NET_HTTP_METHOD_GET, "/text", onStartWork, onWorkStarted);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "HEAD /text HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ(200, statusOf(head));
    EXPECT_EQ("chunked", fieldOf(head, "Transfer-Encoding"));
    ASSERT_TRUE(waitCodes(4));
    expectHealthy(peer);
    const std::vector<KiottyNetCode> expected = { KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_CLOSED,
                                                  KIOTTY_NET_CODE_CLOSED, KIOTTY_NET_CODE_SUCCESS };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, PeerThatLeavesEndsTheStreamWithPeerClosed)
{
    app.job = [](WorkApp& work_app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work) {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        pingUntilFailure(work_app, response, work);
    };
    route(KIOTTY_NET_HTTP_METHOD_GET, "/events", onStartWork, onWorkStarted);
    start();
    {
        kiotty_test::LoopbackPeer peer;
        connect(peer);
        send(peer, "GET /events HTTP/1.1\r\n\r\n");
        std::string head;
        ASSERT_TRUE(readHead(peer, head));
        EXPECT_EQ("chunked", fieldOf(head, "Transfer-Encoding"));
    }
    ASSERT_TRUE(waitCodes(3));
    const std::vector<KiottyNetCode> expected = { KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_PEER_CLOSED,
                                                  KIOTTY_NET_CODE_SUCCESS };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, ServerCloseEndsTheStreamWithClosed)
{
    app.job = [](WorkApp& work_app, KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work) {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        pingUntilFailure(work_app, response, work);
    };
    route(KIOTTY_NET_HTTP_METHOD_GET, "/events", onStartWork, onWorkStarted);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /events HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    std::string line;
    ASSERT_TRUE(readLine(peer, line));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, stop());
    const std::vector<KiottyNetCode> expected = { KIOTTY_NET_CODE_SUCCESS, KIOTTY_NET_CODE_CLOSED,
                                                  KIOTTY_NET_CODE_SUCCESS };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpWorkTest, StreamBodyWithoutWorkIsInternalServerErrorAndCloses)
{
    route(KIOTTY_NET_HTTP_METHOD_GET, "/orphan", onOrphanStream, nullptr);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /orphan HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ(500, statusOf(head));
    EXPECT_EQ("0", fieldOf(head, "Content-Length"));
    EXPECT_EQ("close", fieldOf(head, "Connection"));
    EXPECT_TRUE(isClosedByServer(peer));
}

TEST_F(HttpWorkTest, WorkCallsEnforceTheirRules)
{
    app.job = checkRules;
    route(KIOTTY_NET_HTTP_METHOD_GET, "/rules", onStartWorkTwice, onWorkStarted);
    route(KIOTTY_NET_HTTP_METHOD_GET, "/no-worker", onStartWork, nullptr);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /no-worker HTTP/1.1\r\n\r\n");
    std::string head;
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ(404, statusOf(head));
    send(peer, "GET /rules HTTP/1.1\r\n\r\n");
    ASSERT_TRUE(readHead(peer, head));
    EXPECT_EQ("text/plain; charset=utf-8", fieldOf(head, "Content-Type"));
    const std::vector<std::string> chunks = { "ok", "<end>" };
    EXPECT_EQ(chunks, readChunks(peer));
    ASSERT_TRUE(waitCodes(12));
    const std::vector<KiottyNetCode> expected = {
        KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_SUCCESS,          KIOTTY_NET_CODE_BUSY,
        KIOTTY_NET_CODE_SUCCESS,          KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_INVALID_ARGUMENT,
        KIOTTY_NET_CODE_SUCCESS,          KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_INVALID_ENCODING,
        KIOTTY_NET_CODE_BUSY,             KIOTTY_NET_CODE_SUCCESS,          KIOTTY_NET_CODE_SUCCESS,
    };
    EXPECT_EQ(expected, app.codeList());
}

#endif
