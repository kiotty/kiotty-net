#include "core/kiotty_stage.h"

#include <gtest/gtest.h>

#include <cstring>

using kiotty::Stage;
using kiotty::StageBase;

namespace
{
    void write(StageBase& stage, const char* bytes)
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
    Stage<16> stage;
    write(stage, "hello");
    EXPECT_EQ(stage.consumer().readable(), 5u);
    EXPECT_EQ(std::memcmp(stage.consumer().peek(), "hello", 5), 0);
}

TEST(Stage, ConsumerPeekStaysUntilCommit)
{
    Stage<16> stage;
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
    Stage<16> stage;
    char* target = stage.provider().prepare(10);
    ASSERT_NE(target, nullptr);
    stage.provider().commit(4);
    EXPECT_EQ(stage.provider().writable(), 12u);
    EXPECT_EQ(stage.consumer().readable(), 4u);
}

TEST(Stage, WritableIsZeroWhenEndReachesCapacity)
{
    Stage<8> stage;
    write(stage, "12345678");
    EXPECT_EQ(stage.provider().writable(), 0u);
    stage.consumer().commit(3);
    EXPECT_EQ(stage.provider().writable(), 0u);
}

TEST(Stage, CompactMovesUnreadBytesToFrontAndRestoresWritable)
{
    Stage<8> stage;
    write(stage, "12345678");
    stage.consumer().commit(5);
    stage.compact();
    EXPECT_EQ(stage.consumer().readable(), 3u);
    EXPECT_EQ(std::memcmp(stage.consumer().peek(), "678", 3), 0);
    EXPECT_EQ(stage.provider().writable(), 5u);
}

TEST(Stage, CompactOnEmptyStageResetsBothPositions)
{
    Stage<8> stage;
    write(stage, "1234");
    stage.consumer().commit(4);
    stage.compact();
    EXPECT_EQ(stage.provider().writable(), 8u);
    EXPECT_EQ(stage.consumer().readable(), 0u);
}

TEST(Stage, PrepareMoreThanWritableReturnsNullAndChangesNothing)
{
    Stage<8> stage;
    write(stage, "123");
    EXPECT_EQ(stage.provider().prepare(6), nullptr);
    EXPECT_EQ(stage.provider().writable(), 5u);
    EXPECT_EQ(stage.consumer().readable(), 3u);
}

TEST(Stage, ProviderCommitBeyondWritableAssertsInDebugAndClampsInRelease)
{
    Stage<8> stage;
    write(stage, "123456");
    EXPECT_DEBUG_DEATH(stage.provider().commit(3), "");
#if defined(NDEBUG)
    EXPECT_EQ(stage.provider().writable(), 0u);
    EXPECT_EQ(stage.consumer().readable(), 8u);
#endif
}

TEST(Stage, ConsumerCommitBeyondReadableAssertsInDebugAndClampsInRelease)
{
    Stage<8> stage;
    write(stage, "123");
    EXPECT_DEBUG_DEATH(stage.consumer().commit(4), "");
#if defined(NDEBUG)
    EXPECT_EQ(stage.consumer().readable(), 0u);
#endif
}

TEST(Stage, ZeroCapacityStageIsValidAndAlwaysFull)
{
    Stage<0> stage;
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
    Stage<1> stage;
    write(stage, "x");
    EXPECT_EQ(stage.provider().writable(), 0u);
    EXPECT_EQ(*stage.consumer().peek(), 'x');
    stage.consumer().commit(1);
    stage.compact();
    EXPECT_EQ(stage.provider().writable(), 1u);
    write(stage, "y");
    EXPECT_EQ(*stage.consumer().peek(), 'y');
}

TEST(Stage, LimitBelowCapacityMakesWritableTheLimit)
{
    Stage<4096> stage;
    EXPECT_EQ(stage.capacity(), 4096u);
    EXPECT_EQ(stage.maximum(), 4096u);
    stage.limit(100);
    EXPECT_EQ(stage.capacity(), 100u);
    EXPECT_EQ(stage.maximum(), 4096u);
    EXPECT_EQ(stage.provider().writable(), 100u);
    EXPECT_EQ(stage.provider().prepare(101), nullptr);
    EXPECT_NE(stage.provider().prepare(100), nullptr);
    stage.limit(0);
    EXPECT_EQ(stage.capacity(), 0u);
    EXPECT_EQ(stage.provider().prepare(1), nullptr);
    EXPECT_EQ(stage.consumer().peek(), nullptr);
}

TEST(Stage, LimitAboveTheMaximumIsClampedToTheMaximum)
{
    Stage<64> stage;
    stage.limit(65);
    EXPECT_EQ(stage.capacity(), 64u);
}

TEST(Stage, LimitOnANonEmptyStageAssertsInDebug)
{
    Stage<64> stage;
    write(stage, "abc");
    EXPECT_DEBUG_DEATH(stage.limit(32), "");
}

TEST(Stage, ClearResetsBothPositionsWithoutTouchingTheLimit)
{
    Stage<64> stage;
    stage.limit(32);
    write(stage, "abcdef");
    stage.consumer().commit(2);
    stage.clear();
    EXPECT_EQ(stage.consumer().readable(), 0u);
    EXPECT_EQ(stage.provider().writable(), 32u);
    EXPECT_EQ(stage.capacity(), 32u);
}

TEST(Stage, StageBaseReferenceSeesTheSameBytesAsTheStage)
{
    Stage<64> stage;
    StageBase& base = stage;
    write(base, "kiotty");
    EXPECT_EQ(stage.consumer().readable(), 6u);
    EXPECT_EQ(std::memcmp(stage.consumer().peek(), "kiotty", 6), 0);
}
