#include "core/pool/kiotty_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using kiotty::BlockClass;
using kiotty::Pool;
using kiotty::PoolBase;
using kiotty::PoolLayout;

namespace
{
    typedef PoolLayout<BlockClass<32, 4>, BlockClass<64, 4>, BlockClass<256, 2>> ThreeClasses;

    bool aligned(const void* block)
    {
        return reinterpret_cast<uintptr_t>(block) % kiotty::POOL_ALIGNMENT == 0;
    }
}

TEST(Pool, AllocatesFromTheSmallestClassThatFitsAndAlignsEveryBlock)
{
    Pool<ThreeClasses> pool;
    void* small = pool.allocate(20);
    void* middle = pool.allocate(33);
    void* large = pool.allocate(200);
    EXPECT_TRUE(aligned(small));
    EXPECT_TRUE(aligned(middle));
    EXPECT_TRUE(aligned(large));
    EXPECT_EQ(pool.reallocate(small, 32), small);
    EXPECT_NE(pool.reallocate(middle, 64), nullptr);
    EXPECT_TRUE(pool.release(small));
    EXPECT_TRUE(pool.release(middle));
    EXPECT_TRUE(pool.release(large));
    EXPECT_EQ(pool.failureCount(), 0u);
}

TEST(Pool, AReleasedBlockIsTheNextOneHandedOut)
{
    Pool<ThreeClasses> pool;
    void* first = pool.allocate(32);
    void* second = pool.allocate(32);
    EXPECT_NE(first, second);
    EXPECT_TRUE(pool.release(first));
    EXPECT_EQ(pool.allocate(8), first);
    EXPECT_TRUE(pool.release(first));
    EXPECT_TRUE(pool.release(second));
}

TEST(Pool, UntouchedBlocksAreHandedOutInAddressOrder)
{
    Pool<ThreeClasses> pool;
    char* first = static_cast<char*>(pool.allocate(32));
    char* second = static_cast<char*>(pool.allocate(32));
    char* third = static_cast<char*>(pool.allocate(32));
    EXPECT_EQ(second - first, 32);
    EXPECT_EQ(third - second, 32);
    pool.release(first);
    pool.release(second);
    pool.release(third);
}

TEST(Pool, AFullClassSpillsIntoTheNextLargerClass)
{
    Pool<ThreeClasses> pool;
    std::vector<void*> blocks;
    for (int i = 0; i < 4; ++i)
    {
        blocks.push_back(pool.allocate(16));
    }
    void* spilled = pool.allocate(16);
    ASSERT_NE(spilled, nullptr);
    EXPECT_EQ(pool.reallocate(spilled, 64), spilled);
    EXPECT_EQ(pool.failureCount(), 0u);
    blocks.push_back(spilled);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        EXPECT_TRUE(pool.release(blocks[i]));
    }
}

TEST(Pool, WhenEveryFittingClassIsFullAllocateReturnsNullAndCountsAFailure)
{
    Pool<ThreeClasses> pool;
    std::vector<void*> blocks;
    for (int i = 0; i < 10; ++i)
    {
        void* block = pool.allocate(24);
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }
    EXPECT_EQ(pool.allocate(24), nullptr);
    EXPECT_EQ(pool.allocate(257), nullptr);
    EXPECT_EQ(pool.failureCount(), 2u);
    EXPECT_TRUE(pool.release(blocks.back()));
    EXPECT_NE(pool.allocate(24), nullptr);
}

TEST(Pool, ClassesMayBeListedInAnyOrder)
{
    typedef PoolLayout<BlockClass<4096, 1>, BlockClass<64, 1>, BlockClass<1024, 1>> Unordered;
    Pool<Unordered> pool;
    void* small = pool.allocate(10);
    void* middle = pool.allocate(1000);
    void* large = pool.allocate(1025);
    ASSERT_NE(small, nullptr);
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(large, nullptr);
    EXPECT_EQ(pool.reallocate(small, 64), small);
    EXPECT_EQ(pool.reallocate(middle, 1024), middle);
    EXPECT_EQ(pool.reallocate(large, 4096), large);
    EXPECT_EQ(pool.allocate(10), nullptr);
}

TEST(Pool, ReleaseRejectsPointersItDidNotHandOut)
{
    Pool<ThreeClasses> pool;
    alignas(16) unsigned char outside[64] = {};
    EXPECT_FALSE(pool.release(outside));
    unsigned char* block = static_cast<unsigned char*>(pool.allocate(64));
    EXPECT_FALSE(pool.release(block + 16));
    EXPECT_TRUE(pool.release(nullptr));
    EXPECT_TRUE(pool.release(block));
}

TEST(Pool, ReallocateMovesTheBytesWhenItGrowsAndReleasesOnZero)
{
    Pool<ThreeClasses> pool;
    char* block = static_cast<char*>(pool.allocate(10));
    std::memcpy(block, "kiotty", 7);
    EXPECT_EQ(pool.reallocate(block, 30), block);
    char* moved = static_cast<char*>(pool.reallocate(block, 50));
    ASSERT_NE(moved, nullptr);
    EXPECT_NE(moved, block);
    EXPECT_STREQ(moved, "kiotty");
    EXPECT_EQ(pool.allocate(10), block);
    EXPECT_EQ(pool.reallocate(moved, 0), nullptr);
    alignas(16) unsigned char outside[64] = {};
    EXPECT_EQ(pool.reallocate(outside, 8), nullptr);
}

TEST(Pool, ConcurrentAllocateAndReleaseNeverHandsOutOneBlockTwice)
{
    typedef PoolLayout<BlockClass<64, 64>> Shared;
    Pool<Shared> pool;
    std::atomic<int> collisions(0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&pool, &collisions, t]() {
            for (int round = 0; round < 2000; ++round)
            {
                unsigned char* block = static_cast<unsigned char*>(pool.allocate(64));
                if (block == nullptr)
                {
                    continue;
                }
                std::memset(block, t, 64);
                for (int i = 0; i < 64; ++i)
                {
                    if (block[i] != static_cast<unsigned char>(t))
                    {
                        collisions.fetch_add(1);
                        break;
                    }
                }
                pool.release(block);
            }
        });
    }
    for (size_t i = 0; i < threads.size(); ++i)
    {
        threads[i].join();
    }
    EXPECT_EQ(collisions.load(), 0);
    EXPECT_EQ(pool.failureCount(), 0u);
}

TEST(Pool, ArenaIsAMemberSoTwoPoolsOfOneLayoutDoNotShareBlocks)
{
    Pool<ThreeClasses> first;
    Pool<ThreeClasses> second;
    void* block = first.allocate(32);
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(first.holds(block));
    EXPECT_FALSE(second.holds(block));
    EXPECT_FALSE(second.release(block));
    EXPECT_TRUE(first.release(block));
    EXPECT_EQ(sizeof(Pool<ThreeClasses>) >= Pool<ThreeClasses>::ARENA_BYTES, true);
    EXPECT_EQ(Pool<ThreeClasses>::ARENA_BYTES, 32u * 4 + 64u * 4 + 256u * 2);
}

TEST(Pool, PoolBaseReferenceAllocatesAndReleasesThroughTheVirtualInterface)
{
    Pool<ThreeClasses> pool;
    PoolBase& base = pool;
    void* block = base.allocate(60);
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(base.holds(block));
    EXPECT_EQ(base.reallocate(block, 64), block);
    EXPECT_TRUE(base.release(block));
    EXPECT_EQ(base.failureCount(), 0u);
}
