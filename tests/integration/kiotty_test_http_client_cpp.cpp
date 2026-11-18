#include "kiotty_net_http.h"

#include "support/kiotty_test_http_site.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const uint32_t WAIT_MS = 5000;
    const uint32_t LONG_CALL_MS = 20000;
    const uint32_t WAKE_BOUND_MS = 2000;
    const uint32_t CLOSE_DELAY_MS = 100;
    const int      LIFETIME_ROUNDS = 64;
    const size_t   READ_CHUNK = 16;
    const size_t   MAX_READS = 10000;

    using kiotty_test::textSpan;

    struct Body
    {
        KiottyNetCode code;
        std::string   text;
    };

    Body readAll(KiottyNet_HttpClient& client)
    {
        Body body = { KIOTTY_NET_CODE_SUCCESS, std::string() };
        char buffer[READ_CHUNK];
        for (size_t reads = 0; reads < MAX_READS; ++reads)
        {
            const KiottyNetResult got = client.readBody(buffer, sizeof(buffer), WAIT_MS);
            body.code = got.code;
            if (got.code != KIOTTY_NET_CODE_SUCCESS || got.bytes == 0)
            {
                break;
            }
            body.text.append(buffer, static_cast<size_t>(got.bytes));
        }
        return body;
    }

    std::string bodyOf(KiottyNet_HttpClient& client, const KiottyNetHttpReply& reply)
    {
        if (reply.code != KIOTTY_NET_CODE_SUCCESS)
        {
            return "code " + std::to_string(static_cast<int>(reply.code));
        }
        const Body body = readAll(client);
        if (body.code != KIOTTY_NET_CODE_SUCCESS)
        {
            return "read code " + std::to_string(static_cast<int>(body.code));
        }
        return body.text;
    }

    class HttpClientCppTest : public ::testing::Test
    {
    protected:
        HttpClientCppTest()
            : context(nullptr)
            , site()
        {
        }

        void SetUp() override
        {
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            site.reset(new kiotty_test::HttpSite(context));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
            ASSERT_TRUE(site->start(limits, nullptr));
        }

        void TearDown() override
        {
            site.reset();
            KiottyNet_releaseContext(context);
        }

        KiottyNetContext                       context;
        std::unique_ptr<kiotty_test::HttpSite> site;
        KiottyNetHttpLimits                    limits;
        KiottyNetClientConfiguration           configuration;
    };
}

TEST_F(HttpClientCppTest, DesignUsageOrderRunsEndToEnd)
{
    KiottyNet_HttpClient client(context, configuration, &limits);
    ASSERT_TRUE(static_cast<bool>(client));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, client.code());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address()));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.header("Authorization", "Bearer abc"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.header("User-Agent", "device/1.0"));

    const std::string request_id = "r-1";
    KiottyNet_HttpHeaders<> headers;
    headers.add("Accept", "application/json").add("X-Request-Id", request_id.c_str());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, headers.code());
    EXPECT_EQ(2u, headers.count());

    KiottyNetHttpReply reply = client.get("/inspect", headers, WAIT_MS);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("auth=Bearer abc|agent=device/1.0|accept=application/json|id=r-1", bodyOf(client, reply));

    const std::string report = "{\"temperature\":36.5}";
    reply = client.post("/echo", KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, textSpan(report), WAIT_MS);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("POST:" + report, bodyOf(client, reply));

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.header("Authorization", nullptr));
    reply = client.get("/inspect", WAIT_MS);
    EXPECT_EQ("auth=|agent=device/1.0|accept=|id=", bodyOf(client, reply));

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.clearHeaders());
    reply = client.get("/inspect", WAIT_MS);
    EXPECT_EQ("auth=|agent=|accept=|id=", bodyOf(client, reply));

    client.disconnect();
    reply = client.get("/echo", WAIT_MS);
    EXPECT_EQ("GET:", bodyOf(client, reply));
}

TEST_F(HttpClientCppTest, OverflowingHeadersAreTableFullAndNothingIsSent)
{
    KiottyNet_HttpClient client(context, configuration, nullptr);
    ASSERT_TRUE(static_cast<bool>(client));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address()));

    KiottyNet_HttpHeaders<2> headers;
    headers.add("Accept", "text/plain").add("X-Request-Id", "r-2").add("X-Dropped", "yes");
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, headers.code());
    EXPECT_EQ(2u, headers.count());
    headers.add("X-Also-Dropped", "yes");
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, headers.code());
    EXPECT_EQ(2u, headers.count());

    const KiottyNetHttpReply refused = client.get("/echo", headers, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, refused.code);
    EXPECT_EQ(0, refused.status);
    EXPECT_EQ(nullptr, refused.headers);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.post("/echo", headers, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan("x"), WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.put("/echo", headers, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan("x"), WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.patch("/echo", headers, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan("x"), WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.del("/echo", headers, WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.head("/echo", headers, WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.options("/echo", headers, WAIT_MS).code);
    const KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_GET, "/echo");
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, client.send(request, headers, WAIT_MS).code);

    for (int method = 0; method < KIOTTY_NET_HTTP_METHOD_COUNT; ++method)
    {
        EXPECT_EQ(0u, site->count(static_cast<KiottyNetHttpMethod>(method)));
    }
    const KiottyNetHttpReply reply = client.get("/echo", WAIT_MS);
    EXPECT_EQ("GET:", bodyOf(client, reply));
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_GET));
}

TEST_F(HttpClientCppTest, HeaderListsOfDifferentSizesGoToTheSameMethod)
{
    KiottyNet_HttpClient client(context, configuration, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address()));

    KiottyNet_HttpHeaders<1> small;
    small.add("Accept", "text/one");
    KiottyNet_HttpHeaders<16> large;
    large.add("Accept", "text/sixteen").add("X-Request-Id", "r-16");
    KiottyNet_HttpHeaders<4> empty;

    EXPECT_EQ("auth=|agent=|accept=text/one|id=", bodyOf(client, client.get("/inspect", small, WAIT_MS)));
    EXPECT_EQ("auth=|agent=|accept=text/sixteen|id=r-16", bodyOf(client, client.get("/inspect", large, WAIT_MS)));
    EXPECT_EQ("auth=|agent=|accept=|id=", bodyOf(client, client.get("/inspect", empty, WAIT_MS)));
}

TEST_F(HttpClientCppTest, SendTakesARequestWithAndWithoutHeaders)
{
    KiottyNet_HttpClient client(context, configuration, &limits);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address()));

    KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_PUT, "/echo");
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    const std::string config = "{\"rate\":2}";
    request.body = textSpan(config);

    KiottyNet_HttpHeaders<> headers;
    headers.add("X-Request-Id", "r-put");
    KiottyNetHttpReply reply = client.send(request, headers, WAIT_MS);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("PUT", kiotty_test::replyHeader(reply, "X-Method"));
    EXPECT_EQ("PUT:" + config, bodyOf(client, reply));

    reply = client.send(request, WAIT_MS);
    EXPECT_EQ("PUT:" + config, bodyOf(client, reply));

    const KiottyNetHttpClientRequest inspect = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_GET, "/inspect");
    reply = client.send(inspect, headers, WAIT_MS);
    EXPECT_EQ("auth=|agent=|accept=|id=r-put", bodyOf(client, reply));

    EXPECT_EQ("DELETE:", bodyOf(client, client.del("/echo", WAIT_MS)));
    EXPECT_EQ("PATCH:p", bodyOf(client, client.patch("/echo", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan("p"), WAIT_MS)));
    EXPECT_EQ("PUT:u", bodyOf(client, client.put("/echo", headers, KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, textSpan("u"), WAIT_MS)));
    const KiottyNetHttpReply options = client.options("/inspect", WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, options.code);
    EXPECT_NE(0u, KiottyNet_getHttpAllowedMethods(&options) & KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_GET));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, readAll(client).code);
    EXPECT_EQ("OPTIONS:", bodyOf(client, client.options("/echo", headers, WAIT_MS)));
    const KiottyNetHttpReply head = client.head("/echo", headers, WAIT_MS);
    EXPECT_EQ(200, head.status);
    EXPECT_EQ("", bodyOf(client, head));
}

TEST_F(HttpClientCppTest, CloseFromAnotherThreadWakesReadBody)
{
    KiottyNet_HttpClient client(context, configuration, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address()));
    const KiottyNetHttpReply reply = client.get("/silent", WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    ASSERT_TRUE(site->waitSilentStarted(WAIT_MS));
    char buffer[64];
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.readBody(buffer, sizeof(buffer), WAIT_MS).code);

    std::thread closer([&client]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(CLOSE_DELAY_MS));
        client.close();
    });
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const KiottyNetResult woken = client.readBody(buffer, sizeof(buffer), LONG_CALL_MS);
    const uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
    closer.join();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_LT(elapsed, WAKE_BOUND_MS);

    site->releaseSilent();
    EXPECT_EQ("GET:", bodyOf(client, client.get("/echo", WAIT_MS)));
}

TEST_F(HttpClientCppTest, DestructorReleasesTheClientEveryRound)
{
    for (int round = 0; round < LIFETIME_ROUNDS; ++round)
    {
        KiottyNet_HttpClient client(context, configuration, &limits);
        ASSERT_TRUE(static_cast<bool>(client)) << round;
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address())) << round;
        ASSERT_EQ("GET:", bodyOf(client, client.get("/echo", WAIT_MS))) << round;
    }
    EXPECT_EQ(static_cast<size_t>(LIFETIME_ROUNDS), site->count(KIOTTY_NET_HTTP_METHOD_GET));
}

TEST_F(HttpClientCppTest, FailedConstructionIsFalseWithTheCode)
{
    KiottyNetHttpLimits unmarked = {};
    KiottyNet_HttpClient client(context, configuration, &unmarked);
    EXPECT_FALSE(static_cast<bool>(client));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, client.code());
    EXPECT_EQ(nullptr, client.handle());
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, client.connect(site->address()));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, client.get("/echo", WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, client.header("X", "y"));
    char buffer[4];
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, client.readBody(buffer, sizeof(buffer), WAIT_MS).code);
    client.disconnect();
    client.close();
}

TEST_F(HttpClientCppTest, UploadInPiecesAndAFileThroughTheWrapper)
{
    KiottyNet_HttpClient client(context, configuration, &limits);
    ASSERT_TRUE(static_cast<bool>(client));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.connect(site->address()));
    KiottyNet_HttpHeaders<> headers;
    headers.add("X-Request-Id", "u-1");

    KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_PUT, "/echo");
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.begin(request, headers, 6, WAIT_MS));
    EXPECT_EQ(3, client.write("abc", 3, WAIT_MS).bytes);
    EXPECT_EQ(3, client.write(textSpan("def"), WAIT_MS).bytes);
    KiottyNetHttpReply reply = client.finish(WAIT_MS);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("PUT:abcdef", bodyOf(client, reply));

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, client.begin(request, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, client.write(textSpan("chunk"), WAIT_MS).code);
    reply = client.finish(WAIT_MS);
    EXPECT_EQ("PUT:chunk", bodyOf(client, reply));

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kiotty_cpp_file_" + std::to_string(stamp) + ".txt");
    {
        std::ofstream out(path, std::ios::binary);
        out << "from a file";
    }
    reply = client.sendFile(request, path.string().c_str(), WAIT_MS);
    EXPECT_EQ("PUT:from a file", bodyOf(client, reply));
    reply = client.sendFile(request, headers, path.string().c_str(), WAIT_MS);
    EXPECT_EQ("PUT:from a file", bodyOf(client, reply));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, client.finish(WAIT_MS).code);
}

#endif
