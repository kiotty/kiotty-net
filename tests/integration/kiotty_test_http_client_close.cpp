#include "kiotty_net_http.h"

#include "support/kiotty_test_http_site.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
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
    const uint32_t LONG_CALL_MS = 20000;
    const uint32_t WAKE_BOUND_MS = 2000;
    const uint32_t CLOSE_DELAY_MS = 100;
    const uint32_t LONG_INTERVAL_MS = 30000;
    const char*    UNROUTABLE_HOST = "10.255.255.1";
    const uint16_t UNROUTABLE_PORT = 81;
    const int      RACE_ROUNDS = 200;

    using kiotty_test::readBody;

    using Clock = std::chrono::steady_clock;

    struct Timed
    {
        KiottyNetCode code;
        uint32_t      elapsed;
    };

    class HttpClientCloseTest : public ::testing::Test
    {
    protected:
        HttpClientCloseTest()
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
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&client_configuration));
        }

        void TearDown() override
        {
            if (client != nullptr) { KiottyNet_destroyHttpClient(context, client); }
            site.reset();
            KiottyNet_releaseContext(context);
        }

        void createClient()
        {
            const KiottyNetHttpClientResult created = KiottyNet_createHttpClient(context, &client_configuration, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            client = created.handle;
        }

        void connectToSite()
        {
            ASSERT_TRUE(site->start(limits, nullptr));
            createClient();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &site->address(), nullptr));
        }

        void expectEchoWorks()
        {
            const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
            EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
        }

        template <typename Call>
        Timed closeDuring(Call call, uint32_t delay_milliseconds)
        {
            std::thread closer([this, delay_milliseconds]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_milliseconds));
                KiottyNet_closeHttpClient(client);
            });
            const Clock::time_point started = Clock::now();
            const KiottyNetCode code = call();
            const Timed timed = { code, kiotty_test::elapsedMilliseconds(started) };
            closer.join();
            return timed;
        }

        KiottyNetContext                       context;
        KiottyNetHttpClientHandle              client;
        std::unique_ptr<kiotty_test::HttpSite> site;
        KiottyNetHttpLimits                    limits;
        KiottyNetClientConfiguration           client_configuration;
    };

    bool endsSoon(kiotty_test::LoopbackPeer& peer)
    {
        const Clock::time_point started = Clock::now();
        char byte = 0;
        while (peer.recvExactly(&byte, 1, WAIT_MS)) {}
        return kiotty_test::elapsedMilliseconds(started) < WAIT_MS;
    }

    KiottyNetAddress tcpAddress(const char* host, uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, host, port));
        return address;
    }
}

TEST_F(HttpClientCloseTest, CloseWakesAReadBlockedOnASilentStreamAndTheClientIsUsableAfter)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setHttpClientHeader(client, "Authorization", "Bearer kept"));
    const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/silent", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    ASSERT_TRUE(site->waitSilentStarted(WAIT_MS));
    char buffer[64];
    const KiottyNetResult opening = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opening.code);
    const Timed woken = closeDuring([&]() {
        return KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), LONG_CALL_MS).code;
    }, CLOSE_DELAY_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_LT(woken.elapsed, WAKE_BOUND_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS).code);
    site->releaseSilent();
    const KiottyNetHttpReply after = KiottyNet_getHttp(client, "/headers", nullptr, 0, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, after.code);
    const std::string host = "127.0.0.1:" + std::to_string(site->port());
    EXPECT_EQ("auth=Bearer kept|host=" + host, readBody(client, WAIT_MS).text);
}

TEST_F(HttpClientCloseTest, CloseWakesARequestWaitingForTheHead)
{
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    createClient();
    const KiottyNetAddress address = tcpAddress("127.0.0.1", peer.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    const Timed woken = closeDuring([&]() {
        return KiottyNet_getHttp(client, "/slow", nullptr, 0, LONG_CALL_MS).code;
    }, CLOSE_DELAY_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_LT(woken.elapsed, WAKE_BOUND_MS);
    EXPECT_TRUE(endsSoon(peer));
    ASSERT_TRUE(peer.reset());
    bool is_answered = false;
    std::thread replier([&]() {
        const std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
        if (!peer.accept(WAIT_MS)) { return; }
        std::string head;
        char next = 0;
        while (head.find("\r\n\r\n") == std::string::npos && peer.recvExactly(&next, 1, WAIT_MS)) { head.push_back(next); }
        is_answered = peer.sendAll(reply.data(), reply.size());
    });
    const KiottyNetHttpReply again = KiottyNet_getHttp(client, "/again", nullptr, 0, WAIT_MS);
    replier.join();
    EXPECT_TRUE(is_answered);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ("ok", readBody(client, WAIT_MS).text);
}

TEST_F(HttpClientCloseTest, CloseWakesAReconnectWaitingBetweenTrials)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientRetry(&client_configuration, 2, LONG_INTERVAL_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientConnectTimeout(&client_configuration, WAIT_MS));
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    createClient();
    const KiottyNetAddress address = tcpAddress("127.0.0.1", peer.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    peer.close();
    KiottyNet_disconnectHttpClient(client);
    const Timed woken = closeDuring([&]() {
        return KiottyNet_getHttp(client, "/echo", nullptr, 0, 0).code;
    }, CLOSE_DELAY_MS * 5);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_LT(woken.elapsed, CLOSE_DELAY_MS * 5 + WAKE_BOUND_MS);
}

TEST_F(HttpClientCloseTest, CloseWakesAConnectInProgress)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientConnectTimeout(&client_configuration, LONG_CALL_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureClientRetry(&client_configuration, 1, 0));
    createClient();
    const KiottyNetAddress address = tcpAddress(UNROUTABLE_HOST, UNROUTABLE_PORT);
    const Timed woken = closeDuring([&]() { return KiottyNet_connectHttpClient(client, &address, nullptr); },
                                    CLOSE_DELAY_MS * 3);
    if (woken.code != KIOTTY_NET_CODE_CLOSED && woken.elapsed < CLOSE_DELAY_MS * 3)
    {
        GTEST_SKIP() << "this network answers an unroutable address at once";
    }
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_LT(woken.elapsed, WAKE_BOUND_MS);
}

TEST_F(HttpClientCloseTest, CloseWithNothingInFlightClosesTheConnectionAndTheNextRequestReopens)
{
    connectToSite();
    expectEchoWorks();
    KiottyNet_closeHttpClient(client);
    KiottyNet_closeHttpClient(client);
    expectEchoWorks();
    expectEchoWorks();
    EXPECT_EQ(2u, site->connections());
    EXPECT_EQ(3u, site->count(KIOTTY_NET_HTTP_METHOD_GET));
    KiottyNet_closeHttpClient(nullptr);
}

TEST_F(HttpClientCloseTest, CloseClosesTheIdleSocketAtOnce)
{
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    createClient();
    const KiottyNetAddress address = tcpAddress("127.0.0.1", peer.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    KiottyNet_closeHttpClient(client);
    EXPECT_TRUE(endsSoon(peer));
}

TEST_F(HttpClientCloseTest, CloseRacingRequestsGivesOnlySuccessOrClosed)
{
    connectToSite();
    std::atomic<bool> is_done(false);
    std::thread closer([&]() {
        while (!is_done.load())
        {
            KiottyNet_closeHttpClient(client);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    for (int round = 0; round < RACE_ROUNDS; ++round)
    {
        const KiottyNetHttpReply reply = KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS);
        const bool is_expected = reply.code == KIOTTY_NET_CODE_SUCCESS || reply.code == KIOTTY_NET_CODE_CLOSED;
        EXPECT_TRUE(is_expected) << reply.code;
        if (reply.code != KIOTTY_NET_CODE_SUCCESS) { continue; }
        const kiotty_test::HttpBody body = readBody(client, WAIT_MS);
        const bool is_read = body.code == KIOTTY_NET_CODE_SUCCESS || body.code == KIOTTY_NET_CODE_CLOSED;
        EXPECT_TRUE(is_read) << body.code;
    }
    is_done.store(true);
    closer.join();
    expectEchoWorks();
}

#endif
