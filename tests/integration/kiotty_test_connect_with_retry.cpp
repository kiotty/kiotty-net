#include "data/infra/socket/kiotty_connect_with_retry.h"
#include "data/infra/socket/kiotty_passive_socket.h"
#include "data/infra/socket/kiotty_socket_transfer.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using kiotty::net::implementation::ActiveSocket;
using kiotty_test::LoopbackPeer;

namespace
{
    typedef kiotty::net::implementation::Result<ActiveSocket, KiottyNetCode> Connected;

    KiottyNetClientConfiguration configOf(KiottyNetTransport transport, const std::string& host, uint16_t port, uint32_t max_trial, uint32_t interval_milliseconds)
    {
        KiottyNetClientConfiguration configuration;
        std::memset(&configuration, 0, sizeof(configuration));
        configuration.address.transport = transport;
        kiotty_test::copyHost(configuration.address.host, sizeof(configuration.address.host), host);
        configuration.address.port = port;
        configuration.connect_timeout_milliseconds = 5000;
        configuration.max_trial = max_trial;
        configuration.trial_interval_milliseconds = interval_milliseconds;
        configuration.no_delay = KIOTTY_NET_NO_DELAY_ON;
        return configuration;
    }
}

TEST(ConnectWithRetry, SucceedsOnFirstTrialWhenServerIsUp)
{
    LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    KiottyNetClientConfiguration configuration = configOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", peer.port(), 1, 0);
    std::atomic<bool> stop(false);
    Connected connected = kiotty::net::implementation::connectWithRetry(configuration, stop);
    ASSERT_TRUE(connected);
    EXPECT_TRUE(connected.value.valid());
    EXPECT_TRUE(kiotty_test::noDelayOf(connected.value.handle()));
}

TEST(ConnectWithRetry, RetriesUpToMaxTrialWithIntervalThenReturnsLastCode)
{
    std::string path = kiotty_test::uniqueSocketPath();
    KiottyNetClientConfiguration configuration = configOf(KIOTTY_NET_TRANSPORT_PIPE, path, 0, 3, 50);
    std::atomic<bool> stop(false);
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Connected connected = kiotty::net::implementation::connectWithRetry(configuration, stop);
    uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
    EXPECT_EQ(connected.code, KIOTTY_NET_CODE_SOCKET_ERROR);
    EXPECT_GE(elapsed, 100u);
}

TEST(ConnectWithRetry, SucceedsOnLaterTrialWhenServerStartsDuringInterval)
{
    uint16_t port = kiotty_test::unusedPort();
    KiottyNetClientConfiguration configuration = configOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", port, 10, 200);
    LoopbackPeer peer;
    std::thread server([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        peer.listenTcp(port);
    });
    std::atomic<bool> stop(false);
    Connected connected = kiotty::net::implementation::connectWithRetry(configuration, stop);
    server.join();
    ASSERT_TRUE(connected);
    EXPECT_TRUE(connected.value.valid());
}

TEST(ConnectWithRetry, StopFlagDuringIntervalAbortsWithClosedWithinOneInterval)
{
    std::string path = kiotty_test::uniqueSocketPath();
    KiottyNetClientConfiguration configuration = configOf(KIOTTY_NET_TRANSPORT_PIPE, path, 0, 3, 1000);
    std::atomic<bool> stop(false);
    std::chrono::steady_clock::time_point stopped_at;
    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        stopped_at = std::chrono::steady_clock::now();
        stop.store(true);
    });
    Connected connected = kiotty::net::implementation::connectWithRetry(configuration, stop);
    std::chrono::steady_clock::time_point returned_at = std::chrono::steady_clock::now();
    stopper.join();
    std::chrono::steady_clock::duration after_stop = returned_at - stopped_at;
    EXPECT_EQ(connected.code, KIOTTY_NET_CODE_CLOSED);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(after_stop).count(), 200);
}

TEST(ConnectWithRetry, MaxTrialZeroIsTreatedAsOneTrial)
{
    std::string path = kiotty_test::uniqueSocketPath();
    KiottyNetClientConfiguration configuration = configOf(KIOTTY_NET_TRANSPORT_PIPE, path, 0, 0, 1000);
    std::atomic<bool> stop(false);
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Connected connected = kiotty::net::implementation::connectWithRetry(configuration, stop);
    EXPECT_EQ(connected.code, KIOTTY_NET_CODE_SOCKET_ERROR);
    EXPECT_LT(kiotty_test::elapsedMilliseconds(started), 900u);
}
