#include "core/kiotty_stage.h"
#include "support/kiotty_test_pool.h"

#include <gtest/gtest.h>

#include <cstring>

using kiotty::Stage;
using kiotty_test::TestPool;

namespace
{
    void write(Stage& stage, const char* bytes)
    {
        size_t length = std::strlen(bytes);
        char* target = stage.provider().prepare(length);
        ASSERT_NE(target, nullptr);
        std::memcpy(target, bytes, length);
        stage.provider().commit(length);
    }
}

TEST(Stage, PrepareThenCommitMakesBytesReadable)
{
    TestPool pool;
    Stage stage(pool, 16);
    write(stage, "hello");
    EXPECT_EQ(stage.consumer().readable(), 5u);
    EXPECT_EQ(std::memcmp(stage.consumer().peek(), "hello", 5), 0);
}

TEST(Stage, ConsumerPeekStaysUntilCommit)
{
    TestPool pool;
    Stage stage(pool, 16);
    write(stage, "abcdef");
    char* first = stage.consumer().peek();
    char* second = stage.consumer().peek();
    EXPECT_EQ(first, second);
    stage.consumer().commit(2);
    EXPECT_EQ(stage.consumer().peek(), first + 2);
    EXPECT_EQ(*stage.consumer().peek(), 'c');
    EXPECT_EQ(stage.consumer().readable(), 4u);
}

TEST(Stage, CommitLessThanPreparedKeepsTheRest)
{
    TestPool pool;
    Stage stage(pool, 16);
    char* target = stage.provider().prepare(10);
    ASSERT_NE(target, nullptr);
    stage.provider().commit(4);
    EXPECT_EQ(stage.provider().writable(), 12u);
    EXPECT_EQ(stage.consumer().readable(), 4u);
}

TEST(Stage, WritableIsZeroWhenEndReachesCapacity)
{
    TestPool pool;
    Stage stage(pool, 8);
    write(stage, "12345678");
    EXPECT_EQ(stage.provider().writable(), 0u);
    stage.consumer().commit(3);
    EXPECT_EQ(stage.provider().writable(), 0u);
}

TEST(Stage, CompactMovesUnreadBytesToFrontAndRestoresWritable)
{
    TestPool pool;
    Stage stage(pool, 8);
    write(stage, "12345678");
    stage.consumer().commit(5);
    stage.compact();
    EXPECT_EQ(stage.consumer().readable(), 3u);
    EXPECT_EQ(std::memcmp(stage.consumer().peek(), "678", 3), 0);
    EXPECT_EQ(stage.provider().writable(), 5u);
}

TEST(Stage, CompactOnEmptyStageResetsBothPositions)
{
    TestPool pool;
    Stage stage(pool, 8);
    write(stage, "1234");
    stage.consumer().commit(4);
    stage.compact();
    EXPECT_EQ(stage.provider().writable(), 8u);
    EXPECT_EQ(stage.consumer().readable(), 0u);
}

TEST(Stage, PrepareMoreThanWritableReturnsNullAndChangesNothing)
{
    TestPool pool;
    Stage stage(pool, 8);
    write(stage, "123");
    EXPECT_EQ(stage.provider().prepare(6), nullptr);
    EXPECT_EQ(stage.provider().writable(), 5u);
    EXPECT_EQ(stage.consumer().readable(), 3u);
}

TEST(Stage, ProviderCommitBeyondWritableAssertsInDebugAndClampsInRelease)
{
    TestPool pool;
    Stage stage(pool, 8);
    write(stage, "123456");
    EXPECT_DEBUG_DEATH(stage.provider().commit(3), "");
#if defined(NDEBUG)
    EXPECT_EQ(stage.provider().writable(), 0u);
    EXPECT_EQ(stage.consumer().readable(), 8u);
#endif
}

TEST(Stage, ConsumerCommitBeyondReadableAssertsInDebugAndClampsInRelease)
{
    TestPool pool;
    Stage stage(pool, 8);
    write(stage, "123");
    EXPECT_DEBUG_DEATH(stage.consumer().commit(4), "");
#if defined(NDEBUG)
    EXPECT_EQ(stage.consumer().readable(), 0u);
#endif
}

TEST(Stage, ZeroCapacityStageIsValidAndAlwaysFull)
{
    TestPool pool;
    Stage stage(pool, 0);
    EXPECT_TRUE(stage.valid());
    EXPECT_EQ(stage.capacity(), 0u);
    EXPECT_EQ(stage.provider().writable(), 0u);
    EXPECT_EQ(stage.consumer().readable(), 0u);
    EXPECT_EQ(stage.provider().prepare(1), nullptr);
    EXPECT_EQ(stage.consumer().peek(), nullptr);
    stage.compact();
    EXPECT_EQ(stage.provider().writable(), 0u);
}

TEST(Stage, SingleByteCapacityRoundTrips)
{
    TestPool pool;
    Stage stage(pool, 1);
    write(stage, "x");
    EXPECT_EQ(stage.provider().writable(), 0u);
    EXPECT_EQ(*stage.consumer().peek(), 'x');
    stage.consumer().commit(1);
    stage.compact();
    EXPECT_EQ(stage.provider().writable(), 1u);
    write(stage, "y");
    EXPECT_EQ(*stage.consumer().peek(), 'y');
}

TEST(Stage, TakesItsBufferFromThePoolAndGivesItBackOnDestruction)
{
    typedef kiotty::PoolLayout<kiotty::BlockClass<64, 1>> OneBlock;
    kiotty::Pool pool(OneBlock::classes(), OneBlock::CLASS_COUNT);
    {
        Stage first(pool, 64);
        EXPECT_TRUE(first.valid());
        Stage second(pool, 64);
        EXPECT_FALSE(second.valid());
        EXPECT_EQ(second.capacity(), 0u);
        EXPECT_EQ(second.provider().prepare(1), nullptr);
    }
    Stage again(pool, 64);
    EXPECT_TRUE(again.valid());
    EXPECT_EQ(pool.failureCount(), 1u);
}
