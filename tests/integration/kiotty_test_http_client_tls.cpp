#include "kiotty_net_http.h"

#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "support/kiotty_test_http_site.h"

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
    const uint32_t KEEP_ALIVE_MS = 150;
    const uint32_t PAST_KEEP_ALIVE_MS = 800;
    const int      KEPT_REQUESTS = 4;
    const size_t   TLS_FILE_BYTES = 40000;

    using kiotty_test::readBody;
    using kiotty_test::replyHeader;
    using kiotty_test::textSpan;

    class HttpClientTlsTest : public ::testing::Test
    {
    protected:
        HttpClientTlsTest()
            : context(nullptr)
            , client(nullptr)
            , site()
        {
        }

        void SetUp() override
        {
            if (!kiotty::net::implementation::tlsSupported()) { GTEST_SKIP() << "built without TLS"; }
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            site.reset(new kiotty_test::HttpSite(context));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpTimeouts(&limits, WAIT_MS, KEEP_ALIVE_MS));
            const std::string certificate = kiotty_test::certificatePath("server.pem");
            const std::string key = kiotty_test::certificatePath("server.key");
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS,
                      KiottyNet_initializeTlsServerConfiguration(&server_tls, certificate.c_str(), key.c_str(), nullptr));
            const std::string trust = kiotty_test::certificatePath("ca.pem");
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsClientConfiguration(&client_tls));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsClientTrustStore(&client_tls, trust.c_str()));
            ASSERT_TRUE(site->start(limits, &server_tls));
            KiottyNetClientConfiguration configuration;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
            const KiottyNetHttpClientResult created = KiottyNet_createHttpClient(context, &configuration, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            client = created.handle;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &site->address(), &client_tls));
        }

        void TearDown() override
        {
            if (context == nullptr) { return; }
            if (client != nullptr) { KiottyNet_destroyHttpClient(context, client); }
            site.reset();
            KiottyNet_releaseContext(context);
        }

        KiottyNetContext                       context;
        KiottyNetHttpClientHandle              client;
        std::unique_ptr<kiotty_test::HttpSite> site;
        KiottyNetHttpLimits                    limits;
        KiottyNetTlsServerConfiguration        server_tls;
        KiottyNetTlsClientConfiguration        client_tls;
    };
}

TEST_F(HttpClientTlsTest, GetAndPostCarryTheirBodiesOverOneKeptConnection)
{
    const std::string sent = "{\"v\":1}";
    const KiottyNetHttpContentType json = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    for (int i = 0; i < KEPT_REQUESTS; ++i)
    {
        const KiottyNetHttpReply got = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, got.code);
        EXPECT_EQ(200, got.status);
        EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
        const KiottyNetHttpReply posted = KiottyNet_postHttp(client, "/echo", nullptr, 0, json, textSpan(sent), WAIT_MS);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, posted.code);
        EXPECT_EQ("POST", replyHeader(posted, "X-Method"));
        EXPECT_EQ("POST:{\"v\":1}", readBody(client, WAIT_MS).text);
    }
    EXPECT_EQ(1u, site->connections());
    EXPECT_EQ(static_cast<size_t>(KEPT_REQUESTS), site->count(KIOTTY_NET_HTTP_METHOD_POST));
}

TEST_F(HttpClientTlsTest, HostCarriesTheNonDefaultPort)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("auth=|host=127.0.0.1:" + std::to_string(site->port()), readBody(client, WAIT_MS).text);
}

TEST_F(HttpClientTlsTest, ChunkedWorkStreamIsReadUntilTheEnd)
{
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/stream", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ("chunked", replyHeader(reply, "Transfer-Encoding"));
    const kiotty_test::HttpBody body = readBody(client, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, body.code);
    EXPECT_EQ("data: one\n\ndata: two\n\ndata: three\n\n", body.text);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
}

TEST_F(HttpClientTlsTest, GetOnAnIdleConnectionTheServerClosedIsSentAgainAndPostIsNot)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    std::this_thread::sleep_for(std::chrono::milliseconds(PAST_KEEP_ALIVE_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(2u, site->count(KIOTTY_NET_HTTP_METHOD_GET));
    EXPECT_EQ(2u, site->connections());
    std::this_thread::sleep_for(std::chrono::milliseconds(PAST_KEEP_ALIVE_MS));
    const std::string sent = "x";
    const KiottyNetHttpContentType text = KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN;
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, KiottyNet_postHttp(client, "/echo", nullptr, 0, text, textSpan(sent), WAIT_MS).code);
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_POST));
}

TEST_F(HttpClientTlsTest, CloseWakesAReadBlockedOnASilentStream)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/silent", nullptr, 0, WAIT_MS).code);
    ASSERT_TRUE(site->waitSilentStarted(WAIT_MS));
    char buffer[64];
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS).code);
    std::thread closer([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(CLOSE_DELAY_MS));
        KiottyNet_closeHttpClient(client);
    });
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const KiottyNetResult woken = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), LONG_CALL_MS);
    const uint32_t elapsed = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    closer.join();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_LT(elapsed, WAKE_BOUND_MS);
    site->releaseSilent();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
}

TEST_F(HttpClientTlsTest, UploadsWithLengthAndChunkedOverOneKeptConnection)
{
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_PUT, "/echo");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_beginHttpRequest(client, &request, nullptr, 0, 6, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeHttpRequestBody(client, "abc", 3, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeHttpRequestBody(client, "def", 3, WAIT_MS).code);
    const KiottyNetHttpReply put = KiottyNet_finishHttpRequest(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, put.code);
    EXPECT_EQ("PUT:abcdef", readBody(client, WAIT_MS).text);
    const KiottyNetHttpClientRequest chunked =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_POST, "/echo");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS,
              KiottyNet_beginHttpRequest(client, &chunked, nullptr, 0, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeHttpRequestBody(client, "gh", 2, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeHttpRequestBody(client, "ij", 2, WAIT_MS).code);
    const KiottyNetHttpReply post = KiottyNet_finishHttpRequest(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, post.code);
    EXPECT_EQ("POST:ghij", readBody(client, WAIT_MS).text);
    EXPECT_EQ(1u, site->connections());
}

TEST_F(HttpClientTlsTest, FileIsSentInPiecesOverTls)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kiotty_tls_file_" + std::to_string(stamp) + ".bin");
    std::string content(TLS_FILE_BYTES, 'a');
    for (size_t i = 0; i < content.size(); ++i) { content[i] = static_cast<char>('a' + i % 26); }
    {
        std::ofstream out(path, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_PUT, "/echo");
    const KiottyNetHttpReply put =
        KiottyNet_sendHttpFile(client, &request, nullptr, 0, path.string().c_str(), WAIT_MS);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, put.code);
    EXPECT_EQ(200, put.status);
    EXPECT_EQ("PUT:" + content, readBody(client, WAIT_MS).text);
    EXPECT_EQ(1u, site->connections());
}

TEST_F(HttpClientTlsTest, BeginOnAnIdleConnectionTheServerClosedOpensANewOne)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    std::this_thread::sleep_for(std::chrono::milliseconds(PAST_KEEP_ALIVE_MS));
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_POST, "/echo");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS,
              KiottyNet_beginHttpRequest(client, &request, nullptr, 0, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeHttpRequestBody(client, "abc", 3, WAIT_MS).code);
    const KiottyNetHttpReply posted = KiottyNet_finishHttpRequest(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, posted.code);
    EXPECT_EQ("POST:abc", readBody(client, WAIT_MS).text);
    EXPECT_EQ(2u, site->connections());
}

#endif
