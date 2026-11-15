#include "kiotty_net_http.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
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
    const size_t   BIG_BODY_BYTES = 1000;
    const size_t   MAX_READS = 100000;

    struct App
    {
        App()
            : mutex()
            , ports()
            , workers()
        {
        }

        void addPort(int32_t port)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            ports.push_back(port);
        }

        std::vector<int32_t> portList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return ports;
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

        std::mutex               mutex;
        std::vector<int32_t>     ports;
        std::vector<std::thread> workers;
    };

    struct Body
    {
        KiottyNetCode code;
        std::string   text;
        size_t        reads;
    };

    App& appOf(KiottyNetAppContext context)
    {
        return *static_cast<App*>(context);
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

    std::string bigText()
    {
        std::string text(BIG_BODY_BYTES, 'a');
        for (size_t i = 0; i < text.size(); ++i) { text[i] = static_cast<char>('a' + i % 26); }
        return text;
    }

    std::string methodName(KiottyNetHttpMethod method)
    {
        switch (method)
        {
        case KIOTTY_NET_HTTP_METHOD_GET: return "GET";
        case KIOTTY_NET_HTTP_METHOD_HEAD: return "HEAD";
        case KIOTTY_NET_HTTP_METHOD_POST: return "POST";
        case KIOTTY_NET_HTTP_METHOD_PUT: return "PUT";
        case KIOTTY_NET_HTTP_METHOD_PATCH: return "PATCH";
        case KIOTTY_NET_HTTP_METHOD_DELETE: return "DELETE";
        case KIOTTY_NET_HTTP_METHOD_OPTIONS: return "OPTIONS";
        case KIOTTY_NET_HTTP_METHOD_COUNT: break;
        }
        return "?";
    }

    std::string requestHeader(const KiottyNetHttpRequest* request, const char* name)
    {
        return textOf(KiottyNet_findHttpHeader(request, name));
    }

    void onEcho(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        appOf(app_context).addPort(KiottyNet_getConnectionInfo(request->connection).port);
        const std::string method = methodName(request->method);
        const std::string body = method + ":" + textOf(request->body);
        KiottyNet_setHttpHeader(response, "X-Method", method.c_str());
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, spanOf(body));
    }

    void onHeaders(KiottyNetAppContext, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        const std::string body = "auth=" + requestHeader(request, "Authorization")
                                 + "|agent=" + requestHeader(request, "User-Agent")
                                 + "|trace=" + requestHeader(request, "X-Trace")
                                 + "|host=" + requestHeader(request, "Host");
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, spanOf(body));
    }

    void onBig(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle response)
    {
        appOf(app_context).addPort(KiottyNet_getConnectionInfo(request->connection).port);
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, spanOf(bigText()));
    }

    void onMissing(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_setHttpStatus(response, KIOTTY_NET_HTTP_STATUS_NOT_FOUND);
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_PROBLEM_JSON, spanOf("{\"title\":\"missing\"}"));
    }

    void onStartWork(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_startHttpWork(response);
    }

    void streamEvents(KiottyNetHttpResponseHandle response, KiottyNetHttpWorkHandle work)
    {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        const char* events[] = { "data: one\n\n", "data: two\n\n", "data: three\n\n" };
        for (const char* event : events) { KiottyNet_writeHttpWorkChunk(work, spanOf(event), WAIT_MS); }
        KiottyNet_finishHttpWorkWithResponse(work);
    }

    void onWorkStarted(KiottyNetAppContext app_context,
                       const KiottyNetHttpRequest*,
                       KiottyNetHttpResponseHandle response,
                       KiottyNetHttpWorkHandle work)
    {
        App& app = appOf(app_context);
        const std::lock_guard<std::mutex> lock(app.mutex);
        app.workers.emplace_back([response, work]() { streamEvents(response, work); });
    }

    Body readAll(KiottyNetHttpClientHandle client, size_t chunk)
    {
        Body body = { KIOTTY_NET_CODE_SUCCESS, std::string(), 0 };
        std::vector<char> buffer(chunk);
        while (body.reads < MAX_READS)
        {
            const KiottyNetResult got = KiottyNet_readHttpReplyBody(client, buffer.data(), buffer.size(), WAIT_MS);
            body.reads += 1;
            body.code = got.code;
            if (got.code != KIOTTY_NET_CODE_SUCCESS || got.bytes == 0) { break; }
            body.text.append(buffer.data(), static_cast<size_t>(got.bytes));
        }
        return body;
    }

    std::string replyHeader(const KiottyNetHttpReply& reply, const char* name)
    {
        return textOf(KiottyNet_findHttpReplyHeader(&reply, name));
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

    bool answer(kiotty_test::LoopbackPeer& peer, std::string& head, const std::string& reply)
    {
        return readHead(peer, head) && peer.sendAll(reply.data(), reply.size());
    }

    KiottyNetAddress tcpAddress(uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port));
        return address;
    }

    class HttpClientTest : public ::testing::Test
    {
    protected:
        HttpClientTest()
            : context(nullptr)
            , server(nullptr)
            , client(nullptr)
            , port(0)
            , app()
            , _address()
            , _thread()
        {
        }

        void SetUp() override
        {
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&server_configuration));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&server_configuration, 4));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&client_configuration));
        }

        void TearDown() override
        {
            if (client != nullptr) { KiottyNet_destroyHttpClient(context, client); }
            if (server != nullptr) { KiottyNet_closeHttpServer(server); }
            if (_thread.joinable()) { _thread.join(); }
            app.join();
            if (server != nullptr) { KiottyNet_destroyHttpServer(context, server); }
            KiottyNet_releaseContext(context);
        }

        void startServer()
        {
            const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &server_configuration, &limits);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            server = created.handle;
            const KiottyNetHttpMethod methods[] = {
                KIOTTY_NET_HTTP_METHOD_GET,   KIOTTY_NET_HTTP_METHOD_POST,   KIOTTY_NET_HTTP_METHOD_PUT,
                KIOTTY_NET_HTTP_METHOD_PATCH, KIOTTY_NET_HTTP_METHOD_DELETE, KIOTTY_NET_HTTP_METHOD_OPTIONS,
            };
            for (KiottyNetHttpMethod method : methods) { route(method, "/echo", onEcho, nullptr); }
            route(KIOTTY_NET_HTTP_METHOD_GET, "/headers", onHeaders, nullptr);
            route(KIOTTY_NET_HTTP_METHOD_GET, "/big", onBig, nullptr);
            route(KIOTTY_NET_HTTP_METHOD_GET, "/stream", onStartWork, onWorkStarted);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpNotFound(server, KiottyNet_makeHttpRoute(onMissing, nullptr)));
            port = kiotty_test::unusedPort();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, port));
            _thread = std::thread([this]() { KiottyNet_runHttpServer(server, &_address, nullptr, &app); });
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitHttpServer(server, WAIT_MS));
        }

        void route(KiottyNetHttpMethod method,
                   const char* path,
                   KiottyNetHttpRequestAction action,
                   KiottyNetHttpWorkAction work)
        {
            const KiottyNetHttpRoute made = KiottyNet_makeHttpRoute(action, work);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, method, path, made));
        }

        KiottyNetHttpClientHandle createClient(const KiottyNetHttpLimits* client_limits)
        {
            const KiottyNetHttpClientResult created = KiottyNet_createHttpClient(context, &client_configuration, client_limits);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            return created.handle;
        }

        void connectToServer()
        {
            client = createClient(nullptr);
            ASSERT_NE(nullptr, client);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &_address, nullptr));
        }

        void connectToPeer(kiotty_test::LoopbackPeer& peer)
        {
            ASSERT_TRUE(peer.listenTcp(0));
            client = createClient(nullptr);
            ASSERT_NE(nullptr, client);
            const KiottyNetAddress address = tcpAddress(peer.port());
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
            ASSERT_TRUE(peer.accept(WAIT_MS));
        }

        int32_t lastPort()
        {
            const std::vector<int32_t> ports = app.portList();
            return ports.empty() ? -1 : ports.back();
        }

        KiottyNetContext             context;
        KiottyNetHttpServerHandle    server;
        KiottyNetHttpClientHandle    client;
        uint16_t                     port;
        App                          app;
        KiottyNetHttpLimits          limits;
        KiottyNetServerConfiguration server_configuration;
        KiottyNetClientConfiguration client_configuration;

    private:
        KiottyNetAddress _address;
        std::thread      _thread;
    };
}

TEST_F(HttpClientTest, EveryMethodRoundTripsWithItsBody)
{
    startServer();
    connectToServer();
    const std::string sent = "{\"v\":1}";
    const KiottyNetHttpContentType json = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    const KiottyNetHttpClientHandle handle = client;
    const std::vector<std::function<KiottyNetHttpReply()>> requests = {
        [&]() { return KiottyNet_getHttp(handle, "/echo", nullptr, 0, WAIT_MS); },
        [&]() { return KiottyNet_postHttp(handle, "/echo", nullptr, 0, json, spanOf(sent), WAIT_MS); },
        [&]() { return KiottyNet_putHttp(handle, "/echo", nullptr, 0, json, spanOf(sent), WAIT_MS); },
        [&]() { return KiottyNet_patchHttp(handle, "/echo", nullptr, 0, json, spanOf(sent), WAIT_MS); },
        [&]() { return KiottyNet_deleteHttp(handle, "/echo", nullptr, 0, WAIT_MS); },
        [&]() { return KiottyNet_optionsHttp(handle, "/echo", nullptr, 0, WAIT_MS); },
    };
    const char* expected[] = { "GET:", "POST:{\"v\":1}", "PUT:{\"v\":1}", "PATCH:{\"v\":1}", "DELETE:", "OPTIONS:" };
    const char* names[] = { "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS" };
    for (size_t i = 0; i < requests.size(); ++i)
    {
        const KiottyNetHttpReply reply = requests[i]();
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code) << names[i];
        EXPECT_EQ(200, reply.status) << names[i];
        EXPECT_EQ(names[i], replyHeader(reply, "x-method"));
        const Body body = readAll(client, 64);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, body.code) << names[i];
        EXPECT_EQ(expected[i], body.text);
    }
    const KiottyNetHttpReply head = KiottyNet_headHttp(client, "/echo", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, head.code);
    EXPECT_EQ(200, head.status);
    EXPECT_EQ("HEAD", replyHeader(head, "X-Method"));
    EXPECT_EQ("5", replyHeader(head, "Content-Length"));
    const Body nothing = readAll(client, 64);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, nothing.code);
    EXPECT_EQ("", nothing.text);
    KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_POST, "/echo");
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN;
    const std::string greeting = "hi";
    request.body = spanOf(greeting);
    const KiottyNetHttpReply sent_reply = KiottyNet_sendHttpRequest(client, &request, nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, sent_reply.code);
    EXPECT_EQ("POST:hi", readAll(client, 64).text);
}

TEST_F(HttpClientTest, ClientErrorStatusIsSuccessWithItsBody)
{
    startServer();
    connectToServer();
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/nowhere", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(404, reply.status);
    EXPECT_EQ("application/problem+json", replyHeader(reply, "Content-Type"));
    const Body body = readAll(client, 64);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, body.code);
    EXPECT_EQ("{\"title\":\"missing\"}", body.text);
}

TEST_F(HttpClientTest, TinyBufferReadsTheBodyInManyCallsThenEndsForever)
{
    startServer();
    connectToServer();
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/big", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    const Body body = readAll(client, 3);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, body.code);
    EXPECT_EQ(bigText(), body.text);
    EXPECT_GE(body.reads, BIG_BODY_BYTES / 3);
    EXPECT_EQ("1000", replyHeader(reply, "Content-Length"));
    char buffer[3];
    for (int i = 0; i < 2; ++i)
    {
        const KiottyNetResult again = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
        EXPECT_EQ(0, again.bytes);
    }
}

TEST_F(HttpClientTest, KeepAliveCarriesSeveralRequestsOnOneConnection)
{
    startServer();
    connectToServer();
    for (int i = 0; i < 5; ++i)
    {
        const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
        EXPECT_EQ("GET:", readAll(client, 64).text);
    }
    const std::vector<int32_t> ports = app.portList();
    ASSERT_EQ(5u, ports.size());
    for (int32_t seen : ports) { EXPECT_EQ(ports[0], seen); }
}

TEST_F(HttpClientTest, ReplyHeadOverTheLimitsIsBufferFull)
{
    startServer();
    KiottyNetHttpLimits few;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&few));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpHeaderCapacity(&few, 8192, 1));
    client = createClient(&few);
    ASSERT_NE(nullptr, client);
    KiottyNetAddress address = tcpAddress(port);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    const KiottyNetHttpReply crowded = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, crowded.code);
    EXPECT_EQ(0, crowded.status);
    EXPECT_EQ(nullptr, crowded.headers);
    EXPECT_EQ(0u, crowded.header_count);

    KiottyNetHttpLimits small;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&small));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpHeaderCapacity(&small, 64, 32));
    KiottyNetHttpClientHandle tight = createClient(&small);
    ASSERT_NE(nullptr, tight);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(tight, &address, nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, KiottyNet_getHttp(tight, "/echo", nullptr, 0, WAIT_MS).code);
    KiottyNet_destroyHttpClient(context, tight);
}

TEST_F(HttpClientTest, SharedHeadersAreSentOverriddenRemovedAndCleared)
{
    startServer();
    connectToServer();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "Authorization", "Bearer abc"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "User-Agent", "device/1.0"));
    const std::string host = "|host=127.0.0.1:" + std::to_string(port);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("auth=Bearer abc|agent=device/1.0|trace=" + host, readAll(client, 64).text);

    KiottyNetHttpHeader headers[2];
    headers[0] = KiottyNet_makeHttpHeader("authorization", "Bearer other");
    headers[1] = KiottyNet_makeHttpHeader("X-Trace", "  t-1  ");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", headers, 2, WAIT_MS).code);
    EXPECT_EQ("auth=Bearer other|agent=device/1.0|trace=t-1" + host, readAll(client, 64).text);

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "Authorization", "Bearer new"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("auth=Bearer new|agent=device/1.0|trace=" + host, readAll(client, 64).text);

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "AUTHORIZATION", nullptr));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "X-Absent", nullptr));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("auth=|agent=device/1.0|trace=" + host, readAll(client, 64).text);

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_clearHttpClientHeaders(client));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("auth=|agent=|trace=" + host, readAll(client, 64).text);

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "Authorization", "Bearer kept"));
    KiottyNet_disconnectHttpClient(client);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("auth=Bearer kept|agent=|trace=" + host, readAll(client, 64).text);
}

TEST_F(HttpClientTest, InjectedOrLibraryOwnedHeadersAndBadPathsAreInvalidArgument)
{
    startServer();
    connectToServer();
    const KiottyNetCode invalid = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    EXPECT_EQ(invalid, KiottyNet_setHttpClientHeader(client, "X-Evil", "a\r\nInjected: 1"));
    EXPECT_EQ(invalid, KiottyNet_setHttpClientHeader(client, "Connection", "close"));
    EXPECT_EQ(invalid, KiottyNet_setHttpClientHeader(client, "Bad Name", "x"));
    EXPECT_EQ(invalid, KiottyNet_setHttpClientHeader(client, nullptr, "x"));
    const KiottyNetHttpHeader evil[] = {
        KiottyNet_makeHttpHeader("X-Evil", "a\nInjected: 1"),
        KiottyNet_makeHttpHeader("Host", "elsewhere"),
        KiottyNet_makeHttpHeader("Content-Length", "5"),
        KiottyNet_makeHttpHeader("Transfer-Encoding", "chunked"),
        KiottyNet_makeHttpHeader(nullptr, "x"),
        KiottyNet_makeHttpHeader("X-Null", nullptr),
    };
    for (const KiottyNetHttpHeader& header : evil)
    {
        EXPECT_EQ(invalid, KiottyNet_getHttp(client, "/echo", &header, 1, WAIT_MS).code);
    }
    EXPECT_EQ(invalid, KiottyNet_getHttp(client, "echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_getHttp(client, "/a b", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_getHttp(client, "*", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_getHttp(client, nullptr, nullptr, 0, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_getHttp(client, "/echo", nullptr, 1, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_sendHttpRequest(client, nullptr, nullptr, 0, WAIT_MS).code);
    KiottyNetHttpClientRequest unknown = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_COUNT, "/echo");
    EXPECT_EQ(invalid, KiottyNet_sendHttpRequest(client, &unknown, nullptr, 0, WAIT_MS).code);
    const char broken[] = { '\xC3', '\x28', '\0' };
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING,
              KiottyNet_postHttp(client, "/echo", nullptr, 0, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf(broken), WAIT_MS).code);
    EXPECT_TRUE(app.portList().empty());
    const KiottyNetHttpReply fine = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fine.code);
    EXPECT_EQ("GET:", readAll(client, 64).text);
    const KiottyNetHttpReply whole = KiottyNet_optionsHttp(client, "*", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, whole.code);
    EXPECT_EQ(204, whole.status);
}

TEST_F(HttpClientTest, HeadOverTheLimitsIsRequestTooLargeOrTableFullAndSharedStoreFullIsBufferFull)
{
    startServer();
    KiottyNetHttpLimits small;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&small));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpHeaderCapacity(&small, 256, 3));
    client = createClient(&small);
    ASSERT_NE(nullptr, client);
    KiottyNetAddress address = tcpAddress(port);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    const std::string long_value(300, 'v');
    KiottyNetHttpHeader one = KiottyNet_makeHttpHeader("X-Long", long_value.c_str());
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, KiottyNet_getHttp(client, "/echo", &one, 1, WAIT_MS).code);
    const KiottyNetHttpHeader many[] = {
        KiottyNet_makeHttpHeader("X-A", "1"),
        KiottyNet_makeHttpHeader("X-B", "2"),
        KiottyNet_makeHttpHeader("X-C", "3"),
    };
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, KiottyNet_getHttp(client, "/echo", many, 3, WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, KiottyNet_setHttpClientHeader(client, "X-Long", long_value.c_str()));
    EXPECT_TRUE(app.portList().empty());
}

TEST_F(HttpClientTest, CallsFromAnotherThreadWhileOneIsBlockedAreBusy)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::string head;
    std::thread replier([&]() { answer(peer, head, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"); });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/slow", nullptr, 0, WAIT_MS);
    replier.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    KiottyNetResult got = { 0, KIOTTY_NET_CODE_BUSY };
    std::string received;
    std::thread reader([&]() {
        char buffer[16];
        do
        {
            got = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
        } while (got.code == KIOTTY_NET_CODE_BUSY);
        received.assign(buffer, got.bytes > 0 ? static_cast<size_t>(got.bytes) : 0);
    });
    const bool is_busy = kiotty_test::waitUntil([this]() {
        return KiottyNet_setHttpClientHeader(client, "X-Probe", "1") == KIOTTY_NET_CODE_BUSY;
    }, WAIT_MS);
    EXPECT_TRUE(is_busy);
    const KiottyNetAddress address = tcpAddress(peer.port());
    char other[4];
    const KiottyNetResult busy_read = KiottyNet_readHttpReplyBody(client, other, sizeof(other), 10);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, busy_read.code);
    EXPECT_EQ(0, busy_read.bytes);
    const KiottyNetHttpReply busy_reply = KiottyNet_getHttp(client, "/other", nullptr, 0, 10);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, busy_reply.code);
    EXPECT_EQ(0, busy_reply.status);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_clearHttpClientHeaders(client));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_connectHttpClient(client, &address, nullptr));
    KiottyNet_disconnectHttpClient(client);
    ASSERT_TRUE(peer.sendAll("hello", 5));
    reader.join();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, got.code);
    EXPECT_EQ("hello", received);
    char after[4];
    const KiottyNetResult ended = KiottyNet_readHttpReplyBody(client, after, sizeof(after), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, ended.code);
    EXPECT_EQ(0, ended.bytes);
}

TEST_F(HttpClientTest, ChunkedWorkStreamIsReadUntilTheEndAndTheConnectionIsKept)
{
    startServer();
    connectToServer();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readAll(client, 64).text);
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/stream", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("chunked", replyHeader(reply, "Transfer-Encoding"));
    EXPECT_EQ("text/event-stream; charset=utf-8", replyHeader(reply, "Content-Type"));
    const Body body = readAll(client, 5);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, body.code);
    EXPECT_EQ("data: one\n\ndata: two\n\ndata: three\n\n", body.text);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readAll(client, 64).text);
    const std::vector<int32_t> ports = app.portList();
    ASSERT_EQ(2u, ports.size());
    EXPECT_EQ(ports[0], ports[1]);
}

TEST_F(HttpClientTest, ConnectionCloseReplyMakesTheNextRequestReconnect)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::string head;
    std::thread first([&]() { answer(peer, head, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\nok"); });
    const KiottyNetHttpReply closing = KiottyNet_getHttp(client, "/one", nullptr, 0, WAIT_MS);
    first.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, closing.code);
    EXPECT_EQ("ok", readAll(client, 64).text);
    ASSERT_TRUE(peer.reset());
    bool is_accepted = false;
    std::thread second([&]() {
        is_accepted = peer.accept(WAIT_MS);
        if (is_accepted) { answer(peer, head, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ntwo"); }
    });
    const KiottyNetHttpReply again = KiottyNet_getHttp(client, "/two", nullptr, 0, WAIT_MS);
    second.join();
    EXPECT_TRUE(is_accepted);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ("two", readAll(client, 64).text);
    EXPECT_EQ(0u, head.find("GET /two HTTP/1.1\r\n"));
}

TEST_F(HttpClientTest, UnreadBodyIsDroppedWithItsConnectionByTheNextRequest)
{
    startServer();
    connectToServer();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/big", nullptr, 0, WAIT_MS).code);
    char buffer[3];
    const KiottyNetResult part = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, part.code);
    EXPECT_EQ(3, part.bytes);
    const KiottyNetHttpReply next = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, next.code);
    EXPECT_EQ("GET", replyHeader(next, "X-Method"));
    EXPECT_EQ("GET:", readAll(client, 64).text);
    const std::vector<int32_t> ports = app.portList();
    ASSERT_EQ(2u, ports.size());
    EXPECT_NE(ports[0], ports[1]);
}

TEST_F(HttpClientTest, PipeTransportSendsLocalhostAsTheHost)
{
    const std::string path = kiotty_test::uniqueSocketPath();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenUnix(path));
    KiottyNetAddress address;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializePipeAddress(&address, path.c_str()));
    client = createClient(nullptr);
    ASSERT_NE(nullptr, client);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    std::string head;
    std::thread replier([&]() { answer(peer, head, "HTTP/1.1 204 No Content\r\n\r\n"); });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/local", nullptr, 0, WAIT_MS);
    replier.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(204, reply.status);
    EXPECT_NE(std::string::npos, head.find("\r\nHost: localhost\r\n"));
}

TEST_F(HttpClientTest, InterimRepliesAreSkippedAndSwitchingProtocolsIsProtocolError)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::string head;
    const std::string interim = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: </a>\r\n\r\n"
                                "HTTP/1.1 200 OK\r\nX-Final: yes\r\nContent-Length: 2\r\n\r\nok";
    std::thread first([&]() { answer(peer, head, interim); });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/hints", nullptr, 0, WAIT_MS);
    first.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("yes", replyHeader(reply, "X-Final"));
    EXPECT_EQ("", replyHeader(reply, "Link"));
    EXPECT_EQ("ok", readAll(client, 64).text);
    std::thread second([&]() { answer(peer, head, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n"); });
    const KiottyNetHttpReply switching = KiottyNet_getHttp(client, "/upgrade", nullptr, 0, WAIT_MS);
    second.join();
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, switching.code);
    EXPECT_EQ(0, switching.status);
}

TEST_F(HttpClientTest, BodyWithoutLengthEndsWhenTheServerCloses)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::string head;
    std::thread replier([&]() {
        if (answer(peer, head, "HTTP/1.1 200 OK\r\n\r\nabc")) { peer.shutdownWrite(); }
    });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/until-close", nullptr, 0, WAIT_MS);
    replier.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    const Body body = readAll(client, 2);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, body.code);
    EXPECT_EQ("abc", body.text);
}

TEST_F(HttpClientTest, ReadTimeoutKeepsTheBodyAndTheNextReadContinues)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::string head;
    std::thread replier([&]() { answer(peer, head, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nab"); });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/slow", nullptr, 0, WAIT_MS);
    replier.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    char buffer[8];
    std::string first;
    KiottyNetResult got = { 0, KIOTTY_NET_CODE_SUCCESS };
    while (first.size() < 2 && got.code == KIOTTY_NET_CODE_SUCCESS)
    {
        got = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
        first.append(buffer, got.bytes > 0 ? static_cast<size_t>(got.bytes) : 0);
    }
    EXPECT_EQ("ab", first);
    got = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), 50);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, got.code);
    EXPECT_EQ(0, got.bytes);
    ASSERT_TRUE(peer.sendAll("cd", 2));
    const Body rest = readAll(client, 8);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, rest.code);
    EXPECT_EQ("cd", rest.text);
}

TEST_F(HttpClientTest, BodyCutShortByTheServerIsPeerClosed)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::string head;
    std::thread replier([&]() {
        if (answer(peer, head, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc")) { peer.shutdownWrite(); }
    });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/short", nullptr, 0, WAIT_MS);
    replier.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    const Body body = readAll(client, 64);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, body.code);
    EXPECT_EQ("abc", body.text);
    char buffer[4];
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS).code);
}

TEST_F(HttpClientTest, OptionsReplyListsTheAllowedMethods)
{
    startServer();
    connectToServer();
    const KiottyNetHttpReply reply = KiottyNet_optionsHttp(client, "/headers", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(204, reply.status);
    const uint32_t allowed = KiottyNet_getHttpAllowedMethods(&reply);
    EXPECT_NE(0u, allowed & KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_GET));
    EXPECT_NE(0u, allowed & KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_OPTIONS));
    EXPECT_EQ(0u, allowed & KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_POST));
    EXPECT_EQ(0u, KiottyNet_getHttpAllowedMethods(nullptr));
}

TEST_F(HttpClientTest, NeverConnectedOrNullArgumentsAreInvalidArgument)
{
    client = createClient(nullptr);
    ASSERT_NE(nullptr, client);
    const KiottyNetCode invalid = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    EXPECT_EQ(invalid, reply.code);
    EXPECT_EQ(0, reply.status);
    EXPECT_EQ(nullptr, reply.headers);
    char buffer[4];
    const KiottyNetResult nothing = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, nothing.code);
    EXPECT_EQ(0, nothing.bytes);
    EXPECT_EQ(invalid, KiottyNet_readHttpReplyBody(client, nullptr, 4, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_readHttpReplyBody(client, buffer, 0, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_readHttpReplyBody(nullptr, buffer, sizeof(buffer), WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_connectHttpClient(client, nullptr, nullptr));
    EXPECT_EQ(invalid, KiottyNet_connectHttpClient(nullptr, nullptr, nullptr));
    EXPECT_EQ(invalid, KiottyNet_getHttp(nullptr, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ(invalid, KiottyNet_setHttpClientHeader(nullptr, "X", "y"));
    EXPECT_EQ(invalid, KiottyNet_clearHttpClientHeaders(nullptr));
    EXPECT_EQ(nullptr, KiottyNet_findHttpReplyHeader(nullptr, "X").data);
    EXPECT_EQ(nullptr, KiottyNet_findHttpReplyHeader(&reply, "X").data);
    KiottyNet_disconnectHttpClient(nullptr);
    KiottyNet_destroyHttpClient(context, nullptr);
    KiottyNetHttpLimits unmarked = {};
    const KiottyNetHttpClientResult refused = KiottyNet_createHttpClient(context, &client_configuration, &unmarked);
    EXPECT_EQ(invalid, refused.code);
    EXPECT_EQ(nullptr, refused.handle);
    EXPECT_EQ(invalid, KiottyNet_createHttpClient(context, nullptr, nullptr).code);
    KiottyNetAddress unused = tcpAddress(kiotty_test::unusedPort());
    const KiottyNetCode refused_connect = KiottyNet_connectHttpClient(client, &unused, nullptr);
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, refused_connect);
    EXPECT_NE(invalid, refused_connect);
    const KiottyNetHttpReply unreachable = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    EXPECT_EQ(refused_connect, unreachable.code);
}

#endif
