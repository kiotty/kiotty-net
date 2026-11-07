#include "core/pool/kiotty_lazy_reusable_pool.h"
#include "support/kiotty_test_reusable.h"

#include <gtest/gtest.h>

using kiotty::net::implementation::LazyReusablePool;
using kiotty::net::implementation::LazyReusablePoolConfiguration;
using kiotty::net::implementation::PoolHandle;
using kiotty_test::Reusable;
using kiotty_test::failWith;
using kiotty_test::resetReusableCounts;
using kiotty_test::reusableCounts;
using kiotty_test::succeedWith;

namespace
{
    using Pool = LazyReusablePool<Reusable>;

    class ThreeOwners : public Pool
    {
    public:
        ThreeOwners()
        {
            const int code = initialize(LazyReusablePoolConfiguration{ 3 });
            EXPECT_EQ(code, 0);
        }
    };

    class LazyReusablePoolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            resetReusableCounts();
        }
    };
}

TEST_F(LazyReusablePoolTest, InitializeConstructsEveryObjectOfTheConfiguredCapacity)
{
    Pool pool;
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.initialize(LazyReusablePoolConfiguration{ 5 }), 0);
    EXPECT_EQ(pool.capacity(), 5u);
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_EQ(reusableCounts().constructed, 5);
    EXPECT_EQ(reusableCounts().initialized, 0);
}

TEST_F(LazyReusablePoolTest, InitializeRejectsAZeroCapacityAndASecondCall)
{
    Pool pool;
    const int invalid = static_cast<int>(Reusable::INVALID_ARGUMENT);
    EXPECT_EQ(pool.initialize(LazyReusablePoolConfiguration{ 0 }), invalid);
    EXPECT_EQ(pool.initialize(LazyReusablePoolConfiguration{ 2 }), 0);
    EXPECT_EQ(pool.initialize(LazyReusablePoolConfiguration{ 4 }), invalid);
    EXPECT_EQ(pool.capacity(), 2u);
    EXPECT_EQ(reusableCounts().constructed, 2);
}

TEST_F(LazyReusablePoolTest, AnUninitializedPoolIsFull)
{
    Pool pool;
    const Pool::AcquireResult acquired = pool.acquire(succeedWith(1));
    EXPECT_EQ(acquired.code, static_cast<int>(Reusable::TABLE_FULL));
    EXPECT_EQ(reusableCounts().constructed, 0);
}

TEST_F(LazyReusablePoolTest, AcquireInitializesAnObjectAndReleaseResetsItWithoutDestroyingIt)
{
    ThreeOwners pool;
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
    ThreeOwners pool;
    Reusable& first = pool.acquire(succeedWith(1)).value();
    Reusable& second = pool.acquire(succeedWith(2)).value();
    const PoolHandle old_id = second.id();
    pool.release(second);
    Reusable& again = pool.acquire(succeedWith(3)).value();
    EXPECT_EQ(&again, &second);
    EXPECT_NE(&again, &first);
    EXPECT_EQ(again.id().index, old_id.index);
    EXPECT_EQ(again.id().generation, old_id.generation + 1);
    EXPECT_EQ(reusableCounts().constructed, 3);
}

TEST_F(LazyReusablePoolTest, AFailedInitializeLeavesTheObjectReadyForTheNextAcquire)
{
    ThreeOwners pool;
    const ThreeOwners::AcquireResult failed = pool.acquire(failWith(33));
    EXPECT_EQ(failed.code, 33);
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(pool.count(), 0u);
    const ThreeOwners::AcquireResult next = pool.acquire(succeedWith(1));
    ASSERT_TRUE(next);
    EXPECT_EQ(next.value().id().index, 0);
    EXPECT_EQ(next.value().id().generation, 2);
}

TEST_F(LazyReusablePoolTest, WhenEverySlotIsActiveAcquireReturnsTableFull)
{
    ThreeOwners pool;
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(pool.acquire(succeedWith(i)));
    }
    const ThreeOwners::AcquireResult full = pool.acquire(succeedWith(9));
    EXPECT_EQ(full.code, static_cast<int>(Reusable::TABLE_FULL));
}

TEST_F(LazyReusablePoolTest, DestructorResetsActiveObjectsAndDestroysEveryObjectOnce)
{
    {
        ThreeOwners pool;
        Reusable& first = pool.acquire(succeedWith(1)).value();
        (void)pool.acquire(succeedWith(2));
        pool.release(first);
    }
    EXPECT_EQ(reusableCounts().constructed, 3);
    EXPECT_EQ(reusableCounts().destroyed, 3);
    EXPECT_EQ(reusableCounts().resets, 2);
}

TEST_F(LazyReusablePoolTest, ClearResetsActiveObjectsAndKeepsThemForTheNextAcquire)
{
    ThreeOwners pool;
    (void)pool.acquire(succeedWith(1));
    pool.clear();
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(reusableCounts().destroyed, 0);
    EXPECT_EQ(pool.count(), 0u);
    ASSERT_TRUE(pool.acquire(succeedWith(2)));
    EXPECT_EQ(reusableCounts().constructed, 3);
}
