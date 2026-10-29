#include "base/socket/kiotty_socket.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using kiotty::net::ActiveSocket;
using kiotty_test::LoopbackPeer;

namespace
{
    typedef kiotty::Result<KiottyNetCode, ActiveSocket> Connected;

    KiottyNetClientConfig configOf(KiottyNetTransport transport, const std::string& host, uint16_t port, uint32_t max_trial, uint32_t interval_ms)
    {
        KiottyNetClientConfig config;
        std::memset(&config, 0, sizeof(config));
        config.endpoint.transport = transport;
        kiotty_test::copyHost(config.endpoint.host, sizeof(config.endpoint.host), host);
        config.endpoint.port = port;
        config.connect_timeout_ms = 5000;
        config.max_trial = max_trial;
        config.trial_interval_ms = interval_ms;
        config.no_delay = true;
        return config;
    }
}

TEST(ConnectWithRetry, SucceedsOnFirstTrialWhenServerIsUp)
{
    LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    KiottyNetClientConfig config = configOf(KIOTTY_TCP, "127.0.0.1", peer.port(), 1, 0);
    std::atomic<bool> stop(false);
    Connected connected = kiotty::net::connectWithRetry(config, stop);
    ASSERT_TRUE(connected.isOk());
    EXPECT_TRUE(connected.value().valid());
    EXPECT_TRUE(kiotty_test::noDelayOf(connected.value().handle()));
}

TEST(ConnectWithRetry, RetriesUpToMaxTrialWithIntervalThenReturnsLastCode)
{
    std::string path = kiotty_test::uniqueSocketPath();
    KiottyNetClientConfig config = configOf(KIOTTY_PIPE, path, 0, 3, 50);
    std::atomic<bool> stop(false);
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Connected connected = kiotty::net::connectWithRetry(config, stop);
    uint32_t elapsed = kiotty_test::elapsedMs(started);
    EXPECT_EQ(connected.code(), KIOTTY_SOCKET_ERROR);
    EXPECT_GE(elapsed, 100u);
}

TEST(ConnectWithRetry, SucceedsOnLaterTrialWhenServerStartsDuringInterval)
{
    uint16_t port = kiotty_test::unusedPort();
    KiottyNetClientConfig config = configOf(KIOTTY_TCP, "127.0.0.1", port, 10, 200);
    LoopbackPeer peer;
    std::thread server([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        peer.listenTcp(port);
    });
    std::atomic<bool> stop(false);
    Connected connected = kiotty::net::connectWithRetry(config, stop);
    server.join();
    ASSERT_TRUE(connected.isOk());
    EXPECT_TRUE(connected.value().valid());
}

TEST(ConnectWithRetry, StopFlagDuringIntervalAbortsWithClosedWithinOneInterval)
{
    std::string path = kiotty_test::uniqueSocketPath();
    KiottyNetClientConfig config = configOf(KIOTTY_PIPE, path, 0, 3, 1000);
    std::atomic<bool> stop(false);
    std::chrono::steady_clock::time_point stopped_at;
    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        stopped_at = std::chrono::steady_clock::now();
        stop.store(true);
    });
    Connected connected = kiotty::net::connectWithRetry(config, stop);
    std::chrono::steady_clock::time_point returned_at = std::chrono::steady_clock::now();
    stopper.join();
    std::chrono::steady_clock::duration after_stop = returned_at - stopped_at;
    EXPECT_EQ(connected.code(), KIOTTY_CLOSED);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(after_stop).count(), 200);
}

TEST(ConnectWithRetry, MaxTrialZeroIsTreatedAsOneTrial)
{
    std::string path = kiotty_test::uniqueSocketPath();
    KiottyNetClientConfig config = configOf(KIOTTY_PIPE, path, 0, 0, 1000);
    std::atomic<bool> stop(false);
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Connected connected = kiotty::net::connectWithRetry(config, stop);
    EXPECT_EQ(connected.code(), KIOTTY_SOCKET_ERROR);
    EXPECT_LT(kiotty_test::elapsedMs(started), 900u);
}
