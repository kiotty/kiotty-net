#include "core/kiotty_pool.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

using kiotty::Pool;

namespace
{
    const size_t SMALL_POOL_BYTES = Pool::CLASS_COUNT * 64 * 2;
    const size_t DEFAULT_POOL_BYTES = 32 * 64 * 1024;

    bool aligned(const void* block)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(block);
        return address % Pool::ALIGNMENT == 0;
    }
}

TEST(Pool, AllocateReturnsBlockAlignedToSixteen)
{
    Pool pool(DEFAULT_POOL_BYTES);
    size_t sizes[] = { 1, 17, 64, 100, 1000, 5000 };
    for (size_t size : sizes)
    {
        void* block = pool.allocate(size);
        ASSERT_NE(block, nullptr);
        EXPECT_TRUE(aligned(block)) << size;
        pool.release(block);
    }
    EXPECT_EQ(pool.fallbackCount(), 0u);
}

TEST(Pool, ReleasedBlockIsHandedOutAgain)
{
    Pool pool(64 * 1024);
    void* first = pool.allocate(48);
    pool.release(first);
    void* second = pool.allocate(48);
    EXPECT_EQ(first, second);
    pool.release(second);
    EXPECT_EQ(pool.fallbackCount(), 0u);
}

TEST(Pool, ExhaustedSizeClassFallsBackToHeapAndCounts)
{
    Pool pool(SMALL_POOL_BYTES);
    void* first = pool.allocate(64);
    void* second = pool.allocate(64);
    EXPECT_EQ(pool.fallbackCount(), 0u);
    void* third = pool.allocate(64);
    ASSERT_NE(third, nullptr);
    EXPECT_TRUE(aligned(third));
    EXPECT_EQ(pool.fallbackCount(), 1u);
    void* fourth = pool.allocate(64);
    ASSERT_NE(fourth, nullptr);
    EXPECT_EQ(pool.fallbackCount(), 2u);
    pool.release(fourth);
    pool.release(third);
    pool.release(second);
    pool.release(first);
}

TEST(Pool, ReallocatePreservesContentUpToOldSize)
{
    Pool pool(64 * 1024);
    char* small = static_cast<char*>(pool.allocate(40));
    ASSERT_NE(small, nullptr);
    for (int i = 0; i < 40; ++i)
    {
        small[i] = static_cast<char>(i);
    }
    char* large = static_cast<char*>(pool.reallocate(small, 900));
    ASSERT_NE(large, nullptr);
    for (int i = 0; i < 40; ++i)
    {
        EXPECT_EQ(large[i], static_cast<char>(i));
    }
    char* same = static_cast<char*>(pool.reallocate(large, 800));
    EXPECT_EQ(same, large);
    pool.release(same);
    EXPECT_EQ(pool.fallbackCount(), 0u);
}

TEST(Pool, ReallocateOfFallbackBlockPreservesContent)
{
    Pool pool(SMALL_POOL_BYTES);
    void* first = pool.allocate(64);
    void* second = pool.allocate(64);
    char* fallback = static_cast<char*>(pool.allocate(64));
    ASSERT_NE(fallback, nullptr);
    std::memset(fallback, 'k', 64);
    char* grown = static_cast<char*>(pool.reallocate(fallback, 70000));
    ASSERT_NE(grown, nullptr);
    for (int i = 0; i < 64; ++i)
    {
        EXPECT_EQ(grown[i], 'k');
    }
    pool.release(grown);
    pool.release(second);
    pool.release(first);
}

TEST(Pool, ReleaseOfNullDoesNothing)
{
    Pool pool(64 * 1024);
    pool.release(nullptr);
    EXPECT_EQ(pool.fallbackCount(), 0u);
}

TEST(Pool, AllocateZeroReturnsUsableNonNullBlock)
{
    Pool pool(64 * 1024);
    void* block = pool.allocate(0);
    ASSERT_NE(block, nullptr);
    pool.release(block);
    EXPECT_EQ(pool.fallbackCount(), 0u);
}

TEST(Pool, ReleaseOfForeignPointerAssertsInDebugAndIsIgnoredInRelease)
{
    Pool pool(64 * 1024);
    alignas(16) char foreign[64] = {};
    void* inside = foreign + 32;
    EXPECT_DEBUG_DEATH(pool.release(inside), "");
    EXPECT_EQ(pool.fallbackCount(), 0u);
}
