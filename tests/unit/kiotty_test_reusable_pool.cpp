#include "core/pool/kiotty_reusable_pool.h"
#include "support/kiotty_test_reusable.h"

#include <gtest/gtest.h>

#include <memory>

using kiotty::PoolHandle;
using kiotty::ReusablePool;
using kiotty::is_reusable;
using kiotty_test::Reusable;
using kiotty_test::failWith;
using kiotty_test::resetReusableCounts;
using kiotty_test::reusableCounts;
using kiotty_test::succeedWith;

namespace
{
    typedef ReusablePool<Reusable, 3> ThreeSlots;

    struct Copyable
    {
        typedef int code_type;
        typedef int configuration_type;
        int  initialize(PoolHandle, int) { return 0; }
        void reset() noexcept {}
    };

    struct NoReset
    {
        typedef int code_type;
        typedef int configuration_type;
        NoReset() = default;
        NoReset(const NoReset&) = delete;
        NoReset& operator=(const NoReset&) = delete;
        int initialize(PoolHandle, int) { return 0; }
    };

    struct ThrowingReset
    {
        typedef int code_type;
        typedef int configuration_type;
        ThrowingReset() = default;
        ThrowingReset(const ThrowingReset&) = delete;
        ThrowingReset& operator=(const ThrowingReset&) = delete;
        int  initialize(PoolHandle, int) { return 0; }
        void reset() {}
    };

    class ReusablePoolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            resetReusableCounts();
        }
    };
}

TEST_F(ReusablePoolTest, AcquireInitializesAnEmptySlotAndHandsBackAReference)
{
    ThreeSlots pool;
    EXPECT_EQ(reusableCounts().constructed, 3);
    ThreeSlots::AcquireResult acquired = pool.acquire(succeedWith(7));
    ASSERT_TRUE(acquired.isOk());
    Reusable& object = acquired.value();
    EXPECT_EQ(&object, &pool.at(0));
    EXPECT_EQ(object.value(), 7);
    EXPECT_EQ(object.id().generation, 1);
    EXPECT_EQ(object.id().index, 0);
    EXPECT_EQ(reusableCounts().initialized, 1);
    EXPECT_EQ(pool.count(), 1u);
    EXPECT_TRUE(pool.active(0));
    EXPECT_FALSE(pool.active(1));
}

TEST_F(ReusablePoolTest, AcquireWhenInitializeFailsResetsAndReturnsTheCodeAndTheSlotStaysEmpty)
{
    ThreeSlots pool;
    ThreeSlots::AcquireResult failed = pool.acquire(failWith(42));
    EXPECT_FALSE(failed.isOk());
    EXPECT_EQ(failed.code(), 42);
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_FALSE(pool.active(0));
    ThreeSlots::AcquireResult next = pool.acquire(succeedWith(1));
    ASSERT_TRUE(next.isOk());
    EXPECT_EQ(next.value().id().index, 0);
    EXPECT_EQ(next.value().id().generation, 2);
}

TEST_F(ReusablePoolTest, ReleaseResetsAndTheNextAcquireReusesTheSlotWithANewGeneration)
{
    ThreeSlots pool;
    Reusable& first = pool.acquire(succeedWith(1)).value();
    PoolHandle old_id = first.id();
    pool.release(first);
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(pool.count(), 0u);
    Reusable& again = pool.acquire(succeedWith(2)).value();
    EXPECT_EQ(&again, &first);
    EXPECT_EQ(again.id().index, old_id.index);
    EXPECT_EQ(again.id().generation, old_id.generation + 1);
}

TEST_F(ReusablePoolTest, FindRejectsAPastGenerationAndAnEmptySlot)
{
    ThreeSlots pool;
    Reusable& object = pool.acquire(succeedWith(1)).value();
    PoolHandle current = object.id();
    EXPECT_EQ(pool.find(current), &object);
    pool.release(object);
    EXPECT_EQ(pool.find(current), nullptr);
    Reusable& again = pool.acquire(succeedWith(2)).value();
    EXPECT_EQ(pool.find(current), nullptr);
    EXPECT_EQ(pool.find(again.id()), &again);
    PoolHandle outside = { again.id().generation, 3 };
    EXPECT_EQ(pool.find(outside), nullptr);
    PoolHandle negative = { again.id().generation, -1 };
    EXPECT_EQ(pool.find(negative), nullptr);
    PoolHandle empty_slot = { 1, 1 };
    EXPECT_EQ(pool.find(empty_slot), nullptr);
}

TEST_F(ReusablePoolTest, GenerationNeverReturnsToZeroWhileThePoolLives)
{
    ReusablePool<Reusable, 1> pool;
    int32_t previous = 0;
    for (int round = 0; round < 1000; ++round)
    {
        Reusable& object = pool.acquire(succeedWith(round)).value();
        EXPECT_GT(object.id().generation, previous);
        previous = object.id().generation;
        pool.release(object);
    }
}

TEST_F(ReusablePoolTest, WhenEverySlotIsActiveAcquireReturnsTableFull)
{
    ThreeSlots pool;
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(pool.acquire(succeedWith(i)).isOk());
    }
    EXPECT_TRUE(pool.full());
    ThreeSlots::AcquireResult full = pool.acquire(succeedWith(9));
    EXPECT_EQ(full.code(), static_cast<int>(Reusable::TABLE_FULL));
    EXPECT_EQ(reusableCounts().initialized, 3);
}

TEST_F(ReusablePoolTest, DestructorResetsActiveObjectsOnce)
{
    {
        ThreeSlots pool;
        pool.acquire(succeedWith(1));
        pool.acquire(succeedWith(2));
    }
    EXPECT_EQ(reusableCounts().resets, 2);
    EXPECT_EQ(reusableCounts().destroyed, 3);
}

TEST(ReusablePool, IsReusableRejectsCopyableTypesAndTypesWithoutInitializeOrReset)
{
    EXPECT_TRUE(is_reusable<Reusable>::value);
    EXPECT_FALSE(is_reusable<Copyable>::value);
    EXPECT_FALSE(is_reusable<NoReset>::value);
    EXPECT_FALSE(is_reusable<ThrowingReset>::value);
    EXPECT_FALSE(is_reusable<int>::value);
}
