#include "kiotty_net_http.h"

#include "support/kiotty_test_http_site.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const uint32_t WAIT_MS = 5000;
    const uint32_t KEEP_ALIVE_MS = 100;
    const uint32_t PAST_KEEP_ALIVE_MS = 600;
    const uint32_t SHORT_CALL_MS = 300;
    const uint32_t LONG_INTERVAL_MS = 4000;

    using kiotty_test::readBody;
    using kiotty_test::replyHeader;
    using kiotty_test::textSpan;

    void waitPastKeepAlive()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(PAST_KEEP_ALIVE_MS));
    }

    class HttpClientRetryTest : public ::testing::Test
    {
    protected:
        HttpClientRetryTest()
            : context(nullptr)
            , client(nullptr)
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
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpTimeouts(&limits, WAIT_MS, KEEP_ALIVE_MS));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&client_configuration));
        }

        void TearDown() override
        {
            if (client != nullptr) { KiottyNet_destroyHttpClient(context, client); }
            site.reset();
            KiottyNet_releaseContext(context);
        }

        void connectToSite()
        {
            ASSERT_TRUE(site->start(limits, nullptr));
            createClient();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &site->address(), nullptr));
        }

        void createClient()
        {
            const KiottyNetHttpClientResult created = KiottyNet_createHttpClient(context, &client_configuration, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            client = created.handle;
        }

        void getOnce()
        {
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
            ASSERT_EQ("GET:", readBody(client, WAIT_MS).text);
        }

        KiottyNetContext                        context;
        KiottyNetHttpClientHandle               client;
        std::unique_ptr<kiotty_test::HttpSite>  site;
        KiottyNetHttpLimits                     limits;
        KiottyNetClientConfiguration            client_configuration;
    };

    KiottyNetAddress tcpAddress(uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, "127.0.0.1", port));
        return address;
    }
}

TEST_F(HttpClientRetryTest, IdempotentRequestOnAnIdleConnectionTheServerClosedIsSentAgainOnce)
{
    connectToSite();
    getOnce();
    waitPastKeepAlive();
    const KiottyNetHttpReply again = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(2u, site->count(KIOTTY_NET_HTTP_METHOD_GET));
    EXPECT_EQ(2u, site->connections());

    const std::string sent = "{\"v\":1}";
    const KiottyNetHttpContentType json = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    waitPastKeepAlive();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_putHttp(client, "/echo", nullptr, 0, json, textSpan(sent), WAIT_MS).code);
    EXPECT_EQ("PUT:{\"v\":1}", readBody(client, WAIT_MS).text);
    waitPastKeepAlive();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_deleteHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("DELETE:", readBody(client, WAIT_MS).text);
    waitPastKeepAlive();
    const KiottyNetHttpReply head = KiottyNet_headHttp(client, "/echo", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, head.code);
    EXPECT_EQ("HEAD", replyHeader(head, "X-Method"));
    waitPastKeepAlive();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_optionsHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("OPTIONS:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_DELETE));
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_HEAD));
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_OPTIONS));
    EXPECT_EQ(6u, site->connections());
}

TEST_F(HttpClientRetryTest, PostOrPatchOnAnIdleConnectionTheServerClosedIsPeerClosedAndNotSent)
{
    connectToSite();
    getOnce();
    const std::string sent = "{\"v\":1}";
    const KiottyNetHttpContentType json = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    waitPastKeepAlive();
    const KiottyNetHttpReply refused = KiottyNet_postHttp(client, "/echo", nullptr, 0, json, textSpan(sent), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, refused.code);
    EXPECT_EQ(0, refused.status);
    const KiottyNetHttpReply posted = KiottyNet_postHttp(client, "/echo", nullptr, 0, json, textSpan(sent), WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, posted.code);
    EXPECT_EQ("POST:{\"v\":1}", readBody(client, WAIT_MS).text);
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_POST));
    waitPastKeepAlive();
    const KiottyNetHttpReply patched = KiottyNet_patchHttp(client, "/echo", nullptr, 0, json, textSpan(sent), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, patched.code);
    std::this_thread::sleep_for(std::chrono::milliseconds(SHORT_CALL_MS));
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_PATCH));
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_POST));
    EXPECT_EQ(2u, site->connections());
}

TEST_F(HttpClientRetryTest, ConnectionOpenedByConnectAndDroppedWhileIdleIsRetriedForGetOnly)
{
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    createClient();
    const KiottyNetAddress address = tcpAddress(peer.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    ASSERT_TRUE(peer.reset());
    const std::string answer = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    std::thread server([&]() {
        char first = 0;
        const bool is_asked = peer.accept(WAIT_MS) && peer.recvExactly(&first, 1, WAIT_MS);
        if (is_asked) { peer.sendAll(answer.data(), answer.size()); }
    });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    server.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("ok", readBody(client, WAIT_MS).text);

    KiottyNet_disconnectHttpClient(client);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    ASSERT_TRUE(peer.reset());
    const std::string sent = "{}";
    const KiottyNetHttpContentType json = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    const KiottyNetHttpReply posted = KiottyNet_postHttp(client, "/echo", nullptr, 0, json, textSpan(sent), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, posted.code);
    EXPECT_FALSE(peer.accept(SHORT_CALL_MS));
}

TEST_F(HttpClientRetryTest, FailureOnAConnectionTheRequestOpenedIsNotRetried)
{
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    createClient();
    const KiottyNetAddress address = tcpAddress(peer.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    KiottyNet_disconnectHttpClient(client);

    std::thread closer([&]() {
        char first = 0;
        if (peer.accept(WAIT_MS) && peer.recvExactly(&first, 1, WAIT_MS)) { peer.reset(); }
    });
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
    closer.join();
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_NE(KIOTTY_NET_CODE_TIMEOUT, reply.code);
    EXPECT_FALSE(peer.accept(SHORT_CALL_MS));
}

TEST_F(HttpClientRetryTest, CallTimeoutBoundsTheReconnectAndItsRetryInterval)
{
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    createClient();
    KiottyNetAddress address = tcpAddress(peer.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setAddressRetry(&address, 3, LONG_INTERVAL_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    peer.close();
    KiottyNet_disconnectHttpClient(client);
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, SHORT_CALL_MS);
    const uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
    EXPECT_NE(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_GE(elapsed, SHORT_CALL_MS - 50);
    EXPECT_LT(elapsed, LONG_INTERVAL_MS / 2);
}

#endif
