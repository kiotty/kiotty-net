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
    typedef LazyReusablePool<Reusable, 3> ThreeOwners;

    class LazyReusablePoolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            resetReusableCounts();
        }
    };
}

TEST_F(LazyReusablePoolTest, FirstAcquireCreatesTheObjectAndReleaseRetiresItInsteadOfDeletingIt)
{
    ThreeOwners pool;
    EXPECT_EQ(reusableCounts().constructed, 0);
    Reusable& object = pool.acquire(succeedWith(5)).value();
    EXPECT_EQ(reusableCounts().constructed, 1);
    EXPECT_EQ(object.value(), 5);
    EXPECT_EQ(pool.count(), 1u);
    pool.release(object);
    EXPECT_EQ(reusableCounts().resets, 1);
    EXPECT_EQ(reusableCounts().destroyed, 0);
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_EQ(pool.created(), 1u);
}

TEST_F(LazyReusablePoolTest, TheNextAcquireOfASameTypeReusesTheRetiredObject)
{
    ThreeOwners pool;
    Reusable& first = pool.acquire(succeedWith(1)).value();
    PoolHandle old_id = first.id();
    pool.release(first);
    Reusable& again = pool.acquire(succeedWith(2)).value();
    EXPECT_EQ(&again, &first);
    EXPECT_EQ(reusableCounts().constructed, 1);
    EXPECT_EQ(reusableCounts().initialized, 2);
    EXPECT_EQ(again.id().generation, old_id.generation + 1);
}

TEST_F(LazyReusablePoolTest, AFailedInitializeOfANewObjectKeepsItRetired)
{
    ThreeOwners pool;
    ThreeOwners::AcquireResult failed = pool.acquire(failWith(33));
    EXPECT_EQ(failed.code, 33);
    EXPECT_EQ(reusableCounts().constructed, 1);
    EXPECT_EQ(reusableCounts().destroyed, 0);
    EXPECT_EQ(pool.created(), 1u);
    EXPECT_EQ(pool.count(), 0u);
    ASSERT_TRUE(pool.acquire(succeedWith(1)));
    EXPECT_EQ(reusableCounts().constructed, 1);
}

TEST_F(LazyReusablePoolTest, ReserveCreatesThatManyRetiredObjectsUpFront)
{
    ThreeOwners pool;
    EXPECT_EQ(pool.reserve(2), 0);
    EXPECT_EQ(reusableCounts().constructed, 2);
    EXPECT_EQ(pool.created(), 2u);
    ASSERT_TRUE(pool.acquire(succeedWith(1)));
    ASSERT_TRUE(pool.acquire(succeedWith(2)));
    EXPECT_EQ(reusableCounts().constructed, 2);
    ASSERT_TRUE(pool.acquire(succeedWith(3)));
    EXPECT_EQ(reusableCounts().constructed, 3);
    EXPECT_EQ(pool.reserve(4), static_cast<int>(Reusable::TABLE_FULL));
}

TEST_F(LazyReusablePoolTest, WhenEverySlotIsActiveAcquireReturnsTableFull)
{
    ThreeOwners pool;
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(pool.acquire(succeedWith(i)));
    }
    ThreeOwners::AcquireResult full = pool.acquire(succeedWith(9));
    EXPECT_EQ(full.code, static_cast<int>(Reusable::TABLE_FULL));
    EXPECT_EQ(reusableCounts().constructed, 3);
}

TEST_F(LazyReusablePoolTest, DestructorDeletesEveryObjectOnceWhetherActiveOrRetired)
{
    {
        ThreeOwners pool;
        Reusable& first = pool.acquire(succeedWith(1)).value();
        (void)pool.acquire(succeedWith(2));
        (void)pool.acquire(succeedWith(3));
        pool.release(first);
    }
    EXPECT_EQ(reusableCounts().constructed, 3);
    EXPECT_EQ(reusableCounts().destroyed, 3);
    EXPECT_EQ(reusableCounts().resets, 3);
}

TEST_F(LazyReusablePoolTest, ClearDeletesEverythingAndTheNextAcquireCreatesAgain)
{
    ThreeOwners pool;
    (void)pool.acquire(succeedWith(1));
    pool.clear();
    EXPECT_EQ(reusableCounts().destroyed, 1);
    EXPECT_EQ(pool.created(), 0u);
    ASSERT_TRUE(pool.acquire(succeedWith(2)));
    EXPECT_EQ(reusableCounts().constructed, 2);
}
