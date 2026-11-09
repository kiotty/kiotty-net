#include "core/pool/kiotty_sized_reusable_pool.h"
#include "support/kiotty_test_reusable.h"

#include <gtest/gtest.h>

using kiotty::net::implementation::PoolHandle;
using kiotty::net::implementation::REUSABLE_POOL_MAX_SLOTS;
using kiotty::net::implementation::SizedReusablePool;
using kiotty_test::Reusable;
using kiotty_test::resetReusableCounts;
using kiotty_test::reusableCounts;
using kiotty_test::succeedWith;

namespace
{
    using Pool = SizedReusablePool<Reusable>;

    class SizedReusablePoolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            resetReusableCounts();
        }
    };
}

TEST_F(SizedReusablePoolTest, AnUnsizedPoolHasNoSlotAndIsFull)
{
    Pool pool;
    EXPECT_EQ(pool.capacity(), 0u);
    const Pool::AcquireResult acquired = pool.acquire(succeedWith(1));
    EXPECT_EQ(acquired.code, static_cast<int>(Reusable::TABLE_FULL));
}

TEST_F(SizedReusablePoolTest, ResizeConstructsTheSlotsAndRejectsZeroAndTooMany)
{
    Pool pool;
    EXPECT_FALSE(pool.resize(0));
    EXPECT_FALSE(pool.resize(REUSABLE_POOL_MAX_SLOTS + 1));
    EXPECT_TRUE(pool.resize(4));
    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(reusableCounts().constructed, 4);
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(pool.acquire(succeedWith(i)));
    }
    EXPECT_EQ(pool.acquire(succeedWith(9)).code, static_cast<int>(Reusable::TABLE_FULL));
}

TEST_F(SizedReusablePoolTest, ResizeIsRefusedWhileAnObjectIsActive)
{
    Pool pool;
    ASSERT_TRUE(pool.resize(2));
    Reusable& object = pool.acquire(succeedWith(1)).value();
    EXPECT_FALSE(pool.resize(8));
    EXPECT_EQ(pool.capacity(), 2u);
    pool.release(object);
    EXPECT_TRUE(pool.resize(8));
    EXPECT_EQ(pool.capacity(), 8u);
}

TEST_F(SizedReusablePoolTest, ASmallerResizeKeepsTheSlots)
{
    Pool pool;
    ASSERT_TRUE(pool.resize(8));
    EXPECT_TRUE(pool.resize(2));
    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(reusableCounts().constructed, 8);
}

TEST_F(SizedReusablePoolTest, AGrowingResizeKeepsGenerationsSoAnOldHandleStaysStale)
{
    Pool pool;
    ASSERT_TRUE(pool.resize(1));
    Reusable& first = pool.acquire(succeedWith(1)).value();
    const PoolHandle old_id = first.id();
    pool.release(first);
    ASSERT_TRUE(pool.resize(4));
    Reusable& again = pool.acquire(succeedWith(2)).value();
    EXPECT_EQ(again.id().index, old_id.index);
    EXPECT_EQ(again.id().generation, old_id.generation + 1);
    EXPECT_FALSE(pool.get(old_id));
}

TEST_F(SizedReusablePoolTest, DestructorResetsActiveObjectsOnce)
{
    {
        Pool pool;
        ASSERT_TRUE(pool.resize(3));
        (void)pool.acquire(succeedWith(1));
    }
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(reusableCounts().destroyed, 3);
}
