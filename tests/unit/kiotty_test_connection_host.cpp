#include "data/repository/connection/kiotty_connection_host.h"
#include "support/kiotty_test_completion.h"
#include "support/kiotty_test_handler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace net = kiotty::net::implementation;

using kiotty::net::implementation::PoolHandle;
using kiotty_test::ConnectionBench;
using kiotty_test::limitsOf;
using kiotty_test::spanOf;
using kiotty_test::thenHold;
using kiotty_test::thenWork;
namespace completion = kiotty_test::completion;

namespace
{
    net::ConnectionLimits smallLimits()
    {
        return limitsOf(64, 64, 64);
    }

    std::string payloadOf(net::Connection& connection, const net::IoAction& action)
    {
        net::IoPayload payload = connection.resolve(action);
        return std::string(static_cast<const char*>(payload.buffer), payload.length);
    }

    template <size_t N>
    std::string pickedNotification(ConnectionBench<N>& bench, net::Connection& connection)
    {
        net::IoAction write = bench.pick(connection);
        if (write.kind != net::IoActionKind::WRITE)
        {
            return std::string();
        }
        return payloadOf(connection, write);
    }
}

TEST(ConnectionHost, ReleaseResetsInsideTheNotificationLock)
{
    ConnectionBench<1> bench(smallLimits());
    std::atomic<bool> stop(false);
    std::atomic<int64_t> current(0);
    net::Connection& first = bench.open();
    current.store(first.state().id.value());
    std::thread pusher([&]() {
        for (int i = 0; i < 1000; ++i)
        {
            (void)bench.desk.notify(PoolHandle::fromValue(current.load()), spanOf("x"));
        }
        stop.store(true);
    });
    while (!stop.load())
    {
        net::Connection& connection = bench.host.at(0);
        bench.host.release(connection);
        EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
        net::Connection& reopened = bench.open();
        EXPECT_EQ(net::IoActionKind::NONE, bench.pick(reopened).kind);
        current.store(reopened.state().id.value());
    }
    pusher.join();
}

TEST(ConnectionHost, NotifyWithAPastGenerationReturnsPeerClosed)
{
    ConnectionBench<2> bench(smallLimits());
    net::Connection& connection = bench.open();
    PoolHandle old_id = connection.state().id;
    bench.host.release(connection);
    net::Connection& reopened = bench.open();
    ASSERT_EQ(&connection, &reopened);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.notify(old_id, spanOf("x")));
    PoolHandle outside = { reopened.state().id.generation, 7 };
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.notify(outside, spanOf("x")));
    PoolHandle negative = { reopened.state().id.generation, -1 };
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.notify(negative, spanOf("x")));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(reopened).kind);
}

TEST(ConnectionHost, AcquireFailureResetsTheConnectionAndLeavesTheSlotEmpty)
{
    ConnectionBench<1> bench(limitsOf(0, 64, 64));
    net::ConnectionResult failed = bench.acquire();
    EXPECT_FALSE(failed);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, failed.code);
    EXPECT_TRUE(bench.empty());
    EXPECT_EQ(net::Phase::IDLE, bench.host.at(0).state().phase);
    bench.host.initialize(smallLimits(), 1, kiotty_test::cipherOf(smallLimits()));
    net::ConnectionResult acquired = bench.acquire();
    ASSERT_TRUE(acquired);
    EXPECT_EQ(&bench.host.at(0), &acquired.value());
}

TEST(ConnectionHost, AcquireRejectsLimitsAboveTheCompiledMaximum)
{
    ConnectionBench<1> bench(limitsOf(64, net::MemoryLimits::SEND_CAPACITY + 1, 64));
    net::ConnectionResult failed = bench.acquire();
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, failed.code);
    EXPECT_TRUE(bench.empty());
}

TEST(ConnectionHost, AcquireHandsOutEveryIdleSlotThenNull)
{
    ConnectionBench<4> bench(smallLimits());
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(bench.acquire());
    }
    EXPECT_TRUE(bench.full());
    net::ConnectionResult refused = bench.acquire();
    EXPECT_FALSE(refused);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, refused.code);
}

TEST(ConnectionHost, ReleaseMakesTheSlotAcquirableAgainAndCountIsConsistent)
{
    const size_t slots = 4;
    ConnectionBench<slots> bench(smallLimits());
    std::vector<net::Connection*> open;
    std::mt19937 random(1234);
    for (int i = 0; i < 1000; ++i)
    {
        bool acquire = open.empty() || (open.size() < slots && random() % 2 == 0);
        if (acquire)
        {
            net::ConnectionResult acquired = bench.acquire();
            ASSERT_TRUE(acquired);
            open.push_back(&acquired.value());
        }
        else
        {
            size_t index = random() % open.size();
            bench.host.release(*open[index]);
            open.erase(open.begin() + static_cast<std::ptrdiff_t>(index));
        }
        ASSERT_EQ(open.empty(), bench.empty());
        ASSERT_EQ(open.size() == slots, bench.full());
        if (bench.full())
        {
            ASSERT_EQ(KIOTTY_NET_CODE_TABLE_FULL, bench.acquire().code);
        }
    }
}

TEST(ConnectionHost, FindWithCurrentHandleReturnsTheConnection)
{
    ConnectionBench<2> bench(smallLimits());
    (void)bench.acquire();
    net::Connection& connection = bench.open();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.desk.notify(connection.state().id, spanOf("hi")));
    EXPECT_EQ("hi", pickedNotification(bench, connection));
    KiottyNetConnectionInfo info = bench.desk.info(connection.state().id);
    KiottyNetConnectionInfo expected = connection.state().info;
    EXPECT_EQ(expected.port, info.port);
    EXPECT_STREQ(expected.hostname, info.hostname);
}

TEST(ConnectionHost, FindWithStaleGenerationOrIdleSlotOrOutOfRangeSlotReturnsNull)
{
    ConnectionBench<2> bench(smallLimits());
    net::Connection& connection = bench.open();
    PoolHandle stale = connection.state().id;
    stale.generation += 1;
    PoolHandle idle = { 1, 1 };
    PoolHandle outside = { connection.state().id.generation, 2 };
    PoolHandle handles[] = { stale, idle, outside };
    for (const PoolHandle& id : handles)
    {
        EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.notify(id, spanOf("x")));
        EXPECT_EQ(0, bench.desk.info(id).port);
        EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.working(id).code);
        EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.closeStream(id, thenHold(0)));
    }
}

TEST(ConnectionHost, WorkingAndCloseStreamGoThroughTheCurrentHandleOnly)
{
    ConnectionBench<1> bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenWork());
    bench.opened(connection);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, bench.desk.working(connection.state().id).code);
    bench.complete(connection, completion::workReady());
    net::ConnectionResult working = bench.desk.working(connection.state().id);
    ASSERT_TRUE(working);
    EXPECT_EQ(&connection, &working.value());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.desk.closeStream(connection.state().id, thenHold(0)));
    EXPECT_TRUE(connection.flows().work.isFinished());
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, bench.desk.closeStream(connection.state().id, thenHold(0)));
}

TEST(ConnectionHost, BroadcastStagesToEveryAliveConnectionAndSkipsIdleAndClosing)
{
    const size_t slots = 32;
    ConnectionBench<slots> bench(smallLimits());
    std::vector<net::Connection*> connections;
    for (size_t i = 0; i < slots; ++i)
    {
        net::Connection& connection = bench.open();
        bench.opened(connection);
        connections.push_back(&connection);
    }
    for (size_t i = 0; i < slots; i += 4)
    {
        bench.host.release(*connections[i]);
    }
    for (size_t i = 1; i < slots; i += 4)
    {
        connections[i]->close(KIOTTY_NET_CODE_CLOSED, bench.handler);
    }
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.desk.broadcast(spanOf("all")));
    for (size_t i = 0; i < slots; ++i)
    {
        net::Connection& connection = *connections[i];
        if (connection.state().is_alive)
        {
            EXPECT_EQ("all", pickedNotification(bench, connection)) << i;
        }
        else
        {
            EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind) << i;
        }
    }
}

TEST(ConnectionHost, BroadcastReturnsBufferFullAndWritesNowhereWhenOneConnectionCannotTakeIt)
{
    ConnectionBench<3> bench(limitsOf(64, 64, 8));
    net::Connection& first = bench.open();
    net::Connection& second = bench.open();
    net::Connection& third = bench.open();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(second, "123456"));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, bench.desk.broadcast(spanOf("abc")));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(first).kind);
    EXPECT_EQ("123456", pickedNotification(bench, second));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(third).kind);
}

TEST(ConnectionHost, BroadcastWithNoAliveConnectionReturnsSuccess)
{
    ConnectionBench<2> bench(smallLimits());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.desk.broadcast(spanOf("nobody")));
}

TEST(ConnectionHost, AtReturnsIdleSlotsTooSoLoopsNeedNoOccupancyCheck)
{
    ConnectionBench<4> bench(smallLimits());
    bench.open();
    for (int32_t slot = 1; slot < 4; ++slot)
    {
        net::Connection& idle = bench.host.at(slot);
        EXPECT_FALSE(idle.state().isExpired(UINT64_MAX));
        EXPECT_FALSE(idle.flows().work.isReady());
        EXPECT_FALSE(idle.flows().work.isFinished());
        EXPECT_EQ(net::IoActionKind::NONE, bench.pick(idle).kind);
        EXPECT_NE(net::Phase::CLOSING, idle.state().phase);
        EXPECT_EQ(net::IoActionKind::NONE, bench.complete(idle, completion::timeout()).kind);
    }
    EXPECT_EQ(0u, bench.recorder.total());
}

TEST(ConnectionHost, MaxConnectionsBelowCapacityMakesTheHostFullEarly)
{
    ConnectionBench<4> bench(smallLimits());
    bench.host.initialize(smallLimits(), 2, kiotty_test::cipherOf(smallLimits()));
    ASSERT_TRUE(bench.acquire());
    ASSERT_TRUE(bench.acquire());
    EXPECT_TRUE(bench.full());
    net::ConnectionResult refused = bench.acquire();
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, refused.code);
}

TEST(ConnectionHost, ActiveListsOnlyAcquiredSlotsAndReleaseRemovesThem)
{
    ConnectionBench<4> bench(smallLimits());
    EXPECT_EQ(0u, bench.host.active().length);
    net::Connection& first = bench.open();
    net::Connection& second = bench.open();
    net::Connection& third = bench.open();
    ASSERT_EQ(3u, bench.host.active().length);
    bench.host.release(second);
    const net::Span<const int32_t> active = bench.host.active();
    ASSERT_EQ(2u, active.length);
    const bool has_first = active[0] == first.slot() || active[1] == first.slot();
    const bool has_third = active[0] == third.slot() || active[1] == third.slot();
    EXPECT_TRUE(has_first);
    EXPECT_TRUE(has_third);
    bench.host.release(first);
    bench.host.release(third);
    EXPECT_EQ(0u, bench.host.active().length);
}
