#include "core/pool/kiotty_lazy_reusable_pool.h"
#include "support/kiotty_test_reusable.h"

#include <gtest/gtest.h>

using kiotty::net::implementation::LazyReusablePool;
using kiotty::net::implementation::PoolHandle;
using kiotty_test::Reusable;
using kiotty_test::failWith;
using kiotty_test::resetReusableCounts;
using kiotty_test::reusableCounts;
using kiotty_test::succeedWith;

namespace
{
    using Pool = LazyReusablePool<Reusable>;

    class LazyReusablePoolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            resetReusableCounts();
        }
    };
}

TEST_F(LazyReusablePoolTest, AnEmptyPoolConstructsNothing)
{
    Pool pool;
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_EQ(reusableCounts().constructed, 0);
}

TEST_F(LazyReusablePoolTest, AcquireConstructsAnObjectOnlyWhenNoSlotIsFree)
{
    Pool pool;
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(pool.acquire(succeedWith(i)));
    }
    EXPECT_EQ(pool.capacity(), 3u);
    EXPECT_EQ(pool.count(), 3u);
    EXPECT_EQ(reusableCounts().constructed, 3);
    EXPECT_EQ(reusableCounts().initialized, 3);
}

TEST_F(LazyReusablePoolTest, GrowingKeepsTheAddressOfEveryObject)
{
    Pool pool;
    Reusable& first = pool.acquire(succeedWith(1)).value();
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(pool.acquire(succeedWith(i)));
    }
    EXPECT_EQ(first.value(), 1);
    pool.release(first);
    EXPECT_EQ(pool.count(), 32u);
}

TEST_F(LazyReusablePoolTest, AcquireInitializesAnObjectAndReleaseResetsItWithoutDestroyingIt)
{
    Pool pool;
    Reusable& object = pool.acquire(succeedWith(5)).value();
    EXPECT_EQ(object.value(), 5);
    EXPECT_EQ(pool.count(), 1u);
    pool.release(object);
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(reusableCounts().destroyed, 0);
    EXPECT_EQ(pool.count(), 0u);
}

TEST_F(LazyReusablePoolTest, TheNextAcquireReusesTheReleasedObjectFirst)
{
    Pool pool;
    Reusable& first = pool.acquire(succeedWith(1)).value();
    Reusable& second = pool.acquire(succeedWith(2)).value();
    const PoolHandle old_id = second.id();
    pool.release(second);
    Reusable& again = pool.acquire(succeedWith(3)).value();
    EXPECT_EQ(&again, &second);
    EXPECT_NE(&again, &first);
    EXPECT_EQ(again.id().index, old_id.index);
    EXPECT_EQ(again.id().generation, old_id.generation + 1);
    EXPECT_EQ(reusableCounts().constructed, 2);
}

TEST_F(LazyReusablePoolTest, AFailedInitializeLeavesTheObjectReadyForTheNextAcquire)
{
    Pool pool;
    const Pool::AcquireResult failed = pool.acquire(failWith(33));
    EXPECT_EQ(failed.code, 33);
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(pool.count(), 0u);
    const Pool::AcquireResult next = pool.acquire(succeedWith(1));
    ASSERT_TRUE(next);
    EXPECT_EQ(next.value().id().index, 0);
    EXPECT_EQ(next.value().id().generation, 2);
    EXPECT_EQ(reusableCounts().constructed, 1);
}

TEST_F(LazyReusablePoolTest, DestructorResetsActiveObjectsAndDestroysEveryObjectOnce)
{
    {
        Pool pool;
        Reusable& first = pool.acquire(succeedWith(1)).value();
        (void)pool.acquire(succeedWith(2));
        pool.release(first);
    }
    EXPECT_EQ(reusableCounts().constructed, 2);
    EXPECT_EQ(reusableCounts().destroyed, 2);
    EXPECT_EQ(reusableCounts().resets, 2);
}

TEST_F(LazyReusablePoolTest, ClearResetsActiveObjectsAndKeepsThemForTheNextAcquire)
{
    Pool pool;
    (void)pool.acquire(succeedWith(1));
    pool.clear();
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(reusableCounts().destroyed, 0);
    EXPECT_EQ(pool.count(), 0u);
    ASSERT_TRUE(pool.acquire(succeedWith(2)));
    EXPECT_EQ(reusableCounts().constructed, 1);
}

TEST_F(LazyReusablePoolTest, ForEachActiveVisitsOnlyActiveObjects)
{
    Pool pool;
    Reusable& first = pool.acquire(succeedWith(1)).value();
    (void)pool.acquire(succeedWith(2));
    pool.release(first);
    int visited = 0;
    int sum = 0;
    pool.forEachActive([&](Reusable& object) {
        visited += 1;
        sum += object.value();
    });
    EXPECT_EQ(visited, 1);
    EXPECT_EQ(sum, 2);
}
