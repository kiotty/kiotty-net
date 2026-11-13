#include "kiotty_net_http.h"

#include "data/infra/tls/kiotty_tls_profile.h"
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

    struct App
    {
        App()
            : mutex()
            , disconnects()
            , errors()
            , codes()
            , body()
            , query()
            , header()
            , requests(0)
            , previous{ nullptr, 0 }
        {
        }

        void addDisconnect(KiottyNetCode reason)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            disconnects.push_back(reason);
        }

        void addError(KiottyNetCode code)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            errors.push_back(code);
        }

        std::vector<KiottyNetCode> disconnectList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return disconnects;
        }

        std::vector<KiottyNetCode> errorList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return errors;
        }

        std::vector<KiottyNetCode> codeList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return codes;
        }

        std::string textOf(const std::string& field)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return field;
        }

        std::mutex                  mutex;
        std::vector<KiottyNetCode>  disconnects;
        std::vector<KiottyNetCode>  errors;
        std::vector<KiottyNetCode>  codes;
        std::string                 body;
        std::string                 query;
        std::string                 header;
        std::atomic<int>            requests;
        KiottyNetHttpResponseHandle previous;
    };

    App& appOf(KiottyNetAppContext context)
    {
        return *static_cast<App*>(context);
    }

    KiottyNetSpan spanOf(const char* text)
    {
        KiottyNetSpan span = { text, std::strlen(text) };
        return span;
    }

    std::string textOf(KiottyNetSpan span)
    {
        return std::string(span.data, span.length);
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo, KiottyNetCode reason)
    {
        appOf(app_context).addDisconnect(reason);
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo, KiottyNetCode code, const char*)
    {
        appOf(app_context).addError(code);
    }

    void onHealth(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        appOf(app_context).requests.fetch_add(1);
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf("{\"ok\":true}"));
    }

    void onEcho(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        App& app = appOf(app_context);
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.body = textOf(request->body);
        app.query = textOf(request->query);
        app.header = textOf(KiottyNet_findHttpHeader(request, "x-trace"));
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_CREATED);
        KiottyNet_setHttpHeader(response, "X-Echo", "yes");
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, request->body);
    }

    void onNothing(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle)
    {
    }

    void onMissing(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_NOT_FOUND);
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_PROBLEM_JSON, spanOf("{\"title\":\"missing\"}"));
    }

    void onRules(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        App& app = appOf(app_context);
        const std::lock_guard<std::mutex> lock(app.mutex);
        const char broken[] = { '\xC3', '\x28', '\0' };
        app.codes.push_back(KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM, spanOf("x")));
        app.codes.push_back(KiottyNet_setHttpHeader(response, "Content-Type", "text/plain"));
        app.codes.push_back(KiottyNet_setHttpHeader(response, "X-Evil", "a\r\nb"));
        app.codes.push_back(KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf(broken)));
        app.codes.push_back(KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM, spanOf("x")));
        app.codes.push_back(KiottyNet_setHttpStatus(response, 600));
        app.codes.push_back(KiottyNet_setHttpCustomContentType(response, "multipart/related; boundary=b1"));
        app.codes.push_back(KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM, spanOf("--b1--")));
    }

    void onRemember(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        App& app = appOf(app_context);
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.codes.push_back(KiottyNet_setHttpStatus(app.previous, KIOTTY_NET_HTTP_STATUS_OK));
        app.previous = response;
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_NO_CONTENT);
    }

    struct Response
    {
        int         status;
        std::string head;
        std::string body;
    };

    std::string fieldOf(const std::string& head, const std::string& name)
    {
        const std::string key = "\r\n" + name + ": ";
        const size_t start = head.find(key);
        if (start == std::string::npos) { return std::string(); }
        const size_t value = start + key.size();
        return head.substr(value, head.find("\r\n", value) - value);
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

    Response readResponse(kiotty_test::LoopbackPeer& peer, bool is_head_request = false)
    {
        Response response = { 0, std::string(), std::string() };
        if (!readHead(peer, response.head)) { return response; }
        response.status = std::atoi(response.head.substr(9, 3).c_str());
        const std::string length = fieldOf(response.head, "Content-Length");
        const size_t body_length = length.empty() || is_head_request ? 0 : std::strtoul(length.c_str(), nullptr, 10);
        response.body.resize(body_length);
        if (body_length > 0 && !peer.recvExactly(&response.body[0], body_length, WAIT_MS)) { response.status = -1; }
        return response;
    }

    bool isClosedByServer(kiotty_test::LoopbackPeer& peer)
    {
        char next = 0;
        return !peer.recvExactly(&next, 1, WAIT_MS);
    }

    class HttpServerTest : public ::testing::Test
    {
    protected:
        HttpServerTest()
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
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onHttpDisconnected(server, onDisconnected));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_onHttpError(server, onError));
        }

        void route(KiottyNetHttpMethod method, const char* path, KiottyNetHttpRequestAction action)
        {
            const KiottyNetHttpRoute made = KiottyNet_makeHttpRoute(action, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, method, path, made));
        }

        void start(const KiottyNetTlsServerConfiguration* tls = nullptr)
        {
            port = kiotty_test::unusedPort();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, port));
            _thread = std::thread([this, tls]() { _code.store(KiottyNet_runHttpServer(server, &_address, tls, &app)); });
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

        bool waitDisconnects(size_t count)
        {
            return kiotty_test::waitUntil([this, count]() { return app.disconnectList().size() >= count; }, WAIT_MS);
        }

        KiottyNetContext             context;
        KiottyNetHttpServerHandle    server;
        uint16_t                     port;
        App                          app;
        KiottyNetHttpLimits          limits;
        KiottyNetServerConfiguration configuration;

    private:
        KiottyNetAddress           _address;
        std::atomic<KiottyNetCode> _code;
        std::thread                _thread;
    };
}

TEST_F(HttpServerTest, GetRouteAnswersWithLengthTypeAndDate)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\nHost: a\r\n\r\n");
    const Response response = readResponse(peer);
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("{\"ok\":true}", response.body);
    EXPECT_EQ("11", fieldOf(response.head, "Content-Length"));
    EXPECT_EQ("application/json", fieldOf(response.head, "Content-Type"));
    EXPECT_EQ(29u, fieldOf(response.head, "Date").size());
    EXPECT_EQ(0u, response.head.find("HTTP/1.1 200 OK\r\n"));
}

TEST_F(HttpServerTest, PostRouteReadsTheBodyQueryAndHeaders)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/echo", onEcho);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "POST /echo?x=1 HTTP/1.1\r\nX-Trace: t-7\r\nContent-Length: 5\r\n\r\nhello");
    const Response response = readResponse(peer);
    EXPECT_EQ(201, response.status);
    EXPECT_EQ("hello", response.body);
    EXPECT_EQ("yes", fieldOf(response.head, "X-Echo"));
    EXPECT_EQ("text/plain; charset=utf-8", fieldOf(response.head, "Content-Type"));
    EXPECT_EQ("hello", app.textOf(app.body));
    EXPECT_EQ("x=1", app.textOf(app.query));
    EXPECT_EQ("t-7", app.textOf(app.header));
}

TEST_F(HttpServerTest, ChunkedRequestBodyIsJoined)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/echo", onEcho);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2;x=y\r\nde\r\n0\r\nTrailer: z\r\n\r\n");
    const Response response = readResponse(peer);
    EXPECT_EQ(201, response.status);
    EXPECT_EQ("abcde", response.body);
}

TEST_F(HttpServerTest, KeepAliveServesPipelinedRequestsOnOneConnection)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\n\r\nGET /health HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, readResponse(peer).status);
    EXPECT_EQ(200, readResponse(peer).status);
    send(peer, "GET /health HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, readResponse(peer).status);
    EXPECT_EQ(3, app.requests.load());
    EXPECT_TRUE(app.disconnectList().empty());
}

TEST_F(HttpServerTest, ConnectionCloseEndsAfterTheResponseWithSuccess)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\nConnection: close\r\n\r\n");
    const Response response = readResponse(peer);
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("close", fieldOf(response.head, "Connection"));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitDisconnects(1));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, app.disconnectList()[0]);
}

TEST_F(HttpServerTest, HttpOneZeroClosesUnlessKeepAliveIsAsked)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer kept;
    connect(kept);
    send(kept, "GET /health HTTP/1.0\r\nConnection: keep-alive\r\n\r\nGET /health HTTP/1.0\r\n\r\n");
    EXPECT_EQ(200, readResponse(kept).status);
    EXPECT_EQ(200, readResponse(kept).status);
    EXPECT_TRUE(isClosedByServer(kept));
}

TEST_F(HttpServerTest, UnknownPathIsNotFoundAndTheNotFoundRouteCanAnswer)
{
    create();
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /nowhere HTTP/1.1\r\n\r\n");
    const Response plain = readResponse(peer);
    EXPECT_EQ(404, plain.status);
    EXPECT_EQ("0", fieldOf(plain.head, "Content-Length"));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, stop());
    KiottyNet_destroyHttpServer(context, server);
    server = nullptr;

    create();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpNotFound(server, KiottyNet_makeHttpRoute(onMissing, nullptr)));
    start();
    kiotty_test::LoopbackPeer second;
    connect(second);
    send(second, "GET /nowhere HTTP/1.1\r\n\r\n");
    const Response custom = readResponse(second);
    EXPECT_EQ(404, custom.status);
    EXPECT_EQ("application/problem+json", fieldOf(custom.head, "Content-Type"));
}

TEST_F(HttpServerTest, RouteThatSetsNothingAnswersNotFound)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/empty", onNothing);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /empty HTTP/1.1\r\n\r\n");
    EXPECT_EQ(404, readResponse(peer).status);
}

TEST_F(HttpServerTest, MissingMethodIsMethodNotAllowedWithAllow)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/mcp", onHealth);
    route(KIOTTY_NET_HTTP_METHOD_POST, "/mcp", onEcho);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "DELETE /mcp HTTP/1.1\r\n\r\n");
    const Response response = readResponse(peer);
    EXPECT_EQ(405, response.status);
    EXPECT_EQ("GET, HEAD, POST, OPTIONS", fieldOf(response.head, "Allow"));
}

TEST_F(HttpServerTest, OptionsWithoutARouteIsNoContentWithAllow)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/mcp", onEcho);
    route(KIOTTY_NET_HTTP_METHOD_DELETE, "/other", onNothing);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "OPTIONS /mcp HTTP/1.1\r\n\r\nOPTIONS * HTTP/1.1\r\n\r\n");
    const Response path = readResponse(peer);
    EXPECT_EQ(204, path.status);
    EXPECT_EQ("POST, OPTIONS", fieldOf(path.head, "Allow"));
    EXPECT_EQ("", fieldOf(path.head, "Content-Length"));
    const Response whole = readResponse(peer);
    EXPECT_EQ(204, whole.status);
    EXPECT_EQ("POST, DELETE, OPTIONS", fieldOf(whole.head, "Allow"));
}

TEST_F(HttpServerTest, HeadRunsTheGetRouteAndSendsNoBody)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "HEAD /health HTTP/1.1\r\n\r\nGET /health HTTP/1.1\r\n\r\n");
    const Response head = readResponse(peer, true);
    EXPECT_EQ(200, head.status);
    EXPECT_EQ("11", fieldOf(head.head, "Content-Length"));
    const Response get = readResponse(peer);
    EXPECT_EQ(200, get.status);
    EXPECT_EQ("{\"ok\":true}", get.body);
}

TEST_F(HttpServerTest, BodyOverTheLimitIsContentTooLargeAndCloses)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpBodyCapacity(&limits, 4));
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/echo", onEcho);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "POST /echo HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    const Response response = readResponse(peer);
    EXPECT_EQ(413, response.status);
    EXPECT_EQ("close", fieldOf(response.head, "Connection"));
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitDisconnects(1));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, app.disconnectList()[0]);
}

TEST_F(HttpServerTest, ExpectContinueGetsAnInterimResponseBeforeTheBody)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/echo", onEcho);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    std::string interim;
    send(peer, "POST /echo HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n");
    ASSERT_TRUE(readHead(peer, interim));
    EXPECT_EQ("HTTP/1.1 100 Continue\r\n\r\n", interim);
    send(peer, "hello");
    const Response sized = readResponse(peer);
    EXPECT_EQ(201, sized.status);
    EXPECT_EQ("hello", sized.body);
    send(peer, "POST /echo HTTP/1.1\r\nExpect: 100-continue\r\nTransfer-Encoding: chunked\r\n\r\n");
    ASSERT_TRUE(readHead(peer, interim));
    EXPECT_EQ("HTTP/1.1 100 Continue\r\n\r\n", interim);
    send(peer, "3\r\nabc\r\n0\r\n\r\n");
    const Response chunked = readResponse(peer);
    EXPECT_EQ(201, chunked.status);
    EXPECT_EQ("abc", chunked.body);
}

TEST_F(HttpServerTest, ExpectContinueOverTheLimitIsContentTooLargeWithoutContinue)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpBodyCapacity(&limits, 4));
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/echo", onEcho);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "POST /echo HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n");
    const Response response = readResponse(peer);
    EXPECT_EQ(413, response.status);
    EXPECT_TRUE(isClosedByServer(peer));
}

TEST_F(HttpServerTest, ExpectIsIgnoredForHttpOneZeroAndRequestsWithoutABody)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_POST, "/echo", onEcho);
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\nExpect: 100-continue\r\n\r\n");
    EXPECT_EQ(200, readResponse(peer).status);
    send(peer, "POST /echo HTTP/1.0\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\nhello");
    const Response response = readResponse(peer);
    EXPECT_EQ(201, response.status);
    EXPECT_EQ("hello", response.body);
}

TEST_F(HttpServerTest, TooManyHeadersAreHeaderFieldsTooLarge)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpHeaderCapacity(&limits, 1024, 2));
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\nA: 1\r\nB: 2\r\nC: 3\r\n\r\n");
    EXPECT_EQ(431, readResponse(peer).status);
    EXPECT_TRUE(isClosedByServer(peer));
}

TEST_F(HttpServerTest, MalformedRequestsGetTheirAutomaticStatus)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    const std::string long_target = "GET /" + std::string(KIOTTY_NET_MAX_URL_LENGTH, 'a') + " HTTP/1.1\r\n\r\n";
    const std::string requests[] = { long_target, "BREW /pot HTTP/1.1\r\n\r\n", "GET /health HTTP/9.9\r\n\r\n",
                                     "GET /health HTTP/1.1\r\nBad Header: x\r\n\r\n",
                                     "POST /health HTTP/1.1\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n" };
    const int expected[] = { 414, 501, 400, 400, 400 };
    for (size_t i = 0; i < 5; ++i)
    {
        kiotty_test::LoopbackPeer peer;
        connect(peer);
        send(peer, requests[i]);
        EXPECT_EQ(expected[i], readResponse(peer).status) << i;
        EXPECT_TRUE(isClosedByServer(peer)) << i;
    }
}

TEST_F(HttpServerTest, SlowHeadersCloseWithTimeout)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpTimeouts(&limits, 200, 15000));
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\nHost: a\r\n");
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(waitDisconnects(1));
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, app.disconnectList()[0]);
}

TEST_F(HttpServerTest, ConnectionOverMaxClientsWaitsUntilASlotFrees)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&configuration, 1));
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start();
    kiotty_test::LoopbackPeer first;
    connect(first);
    send(first, "GET /health HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, readResponse(first).status);
    kiotty_test::LoopbackPeer second;
    connect(second);
    send(second, "GET /health HTTP/1.1\r\nConnection: close\r\n\r\n");
    first.close();
    EXPECT_EQ(200, readResponse(second).status);
    EXPECT_TRUE(isClosedByServer(second));
}

TEST_F(HttpServerTest, TlsHandshakeFailureIsReportedThroughTheErrorAction)
{
    if (!kiotty::net::implementation::tlsSupported())
    {
        GTEST_SKIP() << "built without TLS";
    }
    const std::string certificate = std::string(KIOTTY_TEST_CERT_DIR) + "/server.pem";
    const std::string key = std::string(KIOTTY_TEST_CERT_DIR) + "/server.key";
    KiottyNetTlsServerConfiguration tls;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&tls, certificate.c_str(), key.c_str(), nullptr));
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    start(&tls);
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /health HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(isClosedByServer(peer));
    ASSERT_TRUE(kiotty_test::waitUntil([this]() { return !app.errorList().empty(); }, WAIT_MS));
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, app.errorList()[0]);
    ASSERT_TRUE(waitDisconnects(1));
}

TEST_F(HttpServerTest, SettersEnforceTheContentTypeAndHeaderRules)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/rules", onRules);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /rules HTTP/1.1\r\n\r\n");
    const Response response = readResponse(peer);
    const std::vector<KiottyNetCode> expected = {
        KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_INVALID_ARGUMENT,
        KIOTTY_NET_CODE_INVALID_ENCODING, KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_INVALID_ARGUMENT,
        KIOTTY_NET_CODE_SUCCESS,          KIOTTY_NET_CODE_SUCCESS,
    };
    EXPECT_EQ(expected, app.codeList());
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("multipart/related; boundary=b1", fieldOf(response.head, "Content-Type"));
    EXPECT_EQ("--b1--", response.body);
}

TEST_F(HttpServerTest, ResponseHandleOfAnEarlierRequestIsPeerClosed)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/remember", onRemember);
    start();
    kiotty_test::LoopbackPeer peer;
    connect(peer);
    send(peer, "GET /remember HTTP/1.1\r\n\r\nGET /remember HTTP/1.1\r\n\r\n");
    EXPECT_EQ(204, readResponse(peer).status);
    EXPECT_EQ(204, readResponse(peer).status);
    const std::vector<KiottyNetCode> codes = app.codeList();
    ASSERT_EQ(2u, codes.size());
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, codes[0]);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, codes[1]);
}

TEST_F(HttpServerTest, RegistrationIsBusyWhileRunningAndDuplicatesAreRefused)
{
    create();
    route(KIOTTY_NET_HTTP_METHOD_GET, "/health", onHealth);
    const KiottyNetHttpRoute made = KiottyNet_makeHttpRoute(onHealth, nullptr);
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/health", made));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_POST, "/health", made));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "health", made));
    start();
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_PUT, "/x", made));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_onHttpError(server, onError));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, stop());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_PUT, "/x", made));
}

TEST_F(HttpServerTest, CreateRefusesLimitsThatWereNotInitialized)
{
    KiottyNetHttpLimits raw = {};
    const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &configuration, &raw);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, created.code);
    EXPECT_EQ(nullptr, created.handle);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_createHttpServer(context, nullptr, &limits).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_configureHttpResponseCapacity(&limits, 16));
}

#endif
