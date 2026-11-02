#include "base/pool/kiotty_static_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using kiotty::BlockClass;
using kiotty::PoolLayout;
using kiotty::StaticPool;
using kiotty::StaticPoolUsage;

namespace
{
    struct SmallestTag;
    struct ReuseTag;
    struct SpillTag;
    struct FullTag;
    struct ForeignTag;
    struct ReallocateTag;
    struct PeakTag;
    struct ThreadTag;
    struct LayoutTag;
    struct UnorderedTag;
    struct EmptyClassTag;

    typedef BlockClass<32, 4> Class32;
    typedef BlockClass<64, 4> Class64;
    typedef BlockClass<256, 2> Class256;

    bool aligned(const void* block)
    {
        return reinterpret_cast<uintptr_t>(block) % kiotty::POOL_ALIGNMENT == 0;
    }
}

TEST(StaticPool, AllocatesFromTheSmallestClassThatFits)
{
    typedef StaticPool<SmallestTag, PoolLayout<Class32, Class64, Class256>> Pool;
    void* small = Pool::allocate(20);
    void* middle = Pool::allocate(33);
    void* large = Pool::allocate(200);
    EXPECT_EQ(Pool::blockBytes(small), 32u);
    EXPECT_EQ(Pool::blockBytes(middle), 64u);
    EXPECT_EQ(Pool::blockBytes(large), 256u);
    EXPECT_TRUE(aligned(small));
    EXPECT_TRUE(aligned(middle));
    EXPECT_TRUE(aligned(large));
    EXPECT_TRUE(Pool::release(small));
    EXPECT_TRUE(Pool::release(middle));
    EXPECT_TRUE(Pool::release(large));
    EXPECT_EQ(Pool::usage(0).live, 0u);
    EXPECT_EQ(Pool::usage(1).live, 0u);
    EXPECT_EQ(Pool::usage(2).live, 0u);
}

TEST(StaticPool, AReleasedBlockIsTheNextOneHandedOut)
{
    typedef StaticPool<ReuseTag, PoolLayout<Class32>> Pool;
    void* first = Pool::allocate(32);
    void* second = Pool::allocate(32);
    EXPECT_NE(first, second);
    EXPECT_TRUE(Pool::release(first));
    EXPECT_EQ(Pool::allocate(8), first);
    EXPECT_TRUE(Pool::release(first));
    EXPECT_TRUE(Pool::release(second));
}

TEST(StaticPool, AFullClassSpillsIntoTheNextLargerClass)
{
    typedef StaticPool<SpillTag, PoolLayout<Class32, Class64>> Pool;
    std::vector<void*> blocks;
    for (int i = 0; i < 4; ++i)
    {
        blocks.push_back(Pool::allocate(16));
    }
    void* spilled = Pool::allocate(16);
    EXPECT_EQ(Pool::blockBytes(spilled), 64u);
    EXPECT_EQ(Pool::usage(0).failures, 0u);
    EXPECT_TRUE(Pool::release(spilled));
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        EXPECT_TRUE(Pool::release(blocks[i]));
    }
}

TEST(StaticPool, WhenEveryFittingClassIsFullAllocateReturnsNullAndCountsAFailure)
{
    typedef StaticPool<FullTag, PoolLayout<Class32, Class64>> Pool;
    std::vector<void*> blocks;
    for (int i = 0; i < 8; ++i)
    {
        void* block = Pool::allocate(24);
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }
    EXPECT_EQ(Pool::allocate(24), nullptr);
    EXPECT_EQ(Pool::allocate(65), nullptr);
    EXPECT_EQ(Pool::usage(0).failures, 1u);
    EXPECT_EQ(Pool::usage(1).failures, 0u);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        EXPECT_TRUE(Pool::release(blocks[i]));
    }
    EXPECT_NE(Pool::allocate(24), nullptr);
}

TEST(StaticPool, ReleaseRejectsPointersItDidNotHandOut)
{
    typedef StaticPool<ForeignTag, PoolLayout<Class64>> Pool;
    alignas(16) unsigned char outside[64] = {};
    EXPECT_FALSE(Pool::release(outside));
    EXPECT_FALSE(Pool::holds(outside));
    unsigned char* block = static_cast<unsigned char*>(Pool::allocate(64));
    EXPECT_FALSE(Pool::release(block + 16));
    EXPECT_TRUE(Pool::release(nullptr));
    EXPECT_TRUE(Pool::release(block));
}

TEST(StaticPool, ReallocateKeepsTheBlockWhenItFitsAndMovesTheBytesWhenItGrows)
{
    typedef StaticPool<ReallocateTag, PoolLayout<Class32, Class64>> Pool;
    char* block = static_cast<char*>(Pool::allocate(10));
    std::memcpy(block, "kiotty", 7);
    EXPECT_EQ(Pool::reallocate(block, 30), block);
    char* moved = static_cast<char*>(Pool::reallocate(block, 50));
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(Pool::blockBytes(moved), 64u);
    EXPECT_STREQ(moved, "kiotty");
    EXPECT_EQ(Pool::usage(0).live, 0u);
    EXPECT_EQ(Pool::reallocate(moved, 0), nullptr);
    EXPECT_EQ(Pool::usage(1).live, 0u);
}

TEST(StaticPool, PeakIsTheMostBlocksLiveAtOnce)
{
    typedef StaticPool<PeakTag, PoolLayout<Class32>> Pool;
    void* a = Pool::allocate(1);
    void* b = Pool::allocate(1);
    void* c = Pool::allocate(1);
    Pool::release(b);
    Pool::release(c);
    void* d = Pool::allocate(1);
    StaticPoolUsage usage = Pool::usage(0);
    EXPECT_EQ(usage.live, 2u);
    EXPECT_EQ(usage.peak, 3u);
    EXPECT_EQ(usage.block_bytes, 32u);
    EXPECT_EQ(usage.block_count, 4u);
    Pool::release(a);
    Pool::release(d);
}

TEST(StaticPool, ConcurrentAllocateAndReleaseNeverHandsOutOneBlockTwice)
{
    typedef StaticPool<ThreadTag, PoolLayout<BlockClass<64, 64>>> Pool;
    std::atomic<int> collisions(0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&collisions, t]() {
            for (int round = 0; round < 2000; ++round)
            {
                unsigned char* block = static_cast<unsigned char*>(Pool::allocate(64));
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
                Pool::release(block);
            }
        });
    }
    for (size_t i = 0; i < threads.size(); ++i)
    {
        threads[i].join();
    }
    EXPECT_EQ(collisions.load(), 0);
    EXPECT_EQ(Pool::usage(0).live, 0u);
    EXPECT_LE(Pool::usage(0).peak, 4u);
}

TEST(StaticPool, LayoutConstantsDescribeTheArena)
{
    typedef StaticPool<LayoutTag, PoolLayout<Class32, Class64, Class256>> Pool;
    size_t classes = Pool::CLASS_COUNT;
    size_t arena = Pool::ARENA_BYTES;
    EXPECT_EQ(classes, 3u);
    EXPECT_EQ(arena, 32u * 4 + 64u * 4 + 256u * 2);
    EXPECT_EQ(Pool::allocate(257), nullptr);
    EXPECT_EQ(Pool::usage(3).block_bytes, 0u);
}

TEST(StaticPool, ClassesMayBeListedInAnyOrder)
{
    typedef StaticPool<UnorderedTag, PoolLayout<BlockClass<4096, 1>, BlockClass<64, 1>, BlockClass<1024, 1>>> Pool;
    void* small = Pool::allocate(10);
    void* middle = Pool::allocate(1000);
    void* large = Pool::allocate(1025);
    EXPECT_EQ(Pool::blockBytes(small), 64u);
    EXPECT_EQ(Pool::blockBytes(middle), 1024u);
    EXPECT_EQ(Pool::blockBytes(large), 4096u);
    EXPECT_EQ(Pool::allocate(10), nullptr);
    EXPECT_EQ(Pool::usage(1).failures, 1u);
    Pool::release(small);
    Pool::release(middle);
    Pool::release(large);
}

TEST(StaticPool, AClassWithNoBlocksIsSkipped)
{
    typedef StaticPool<EmptyClassTag, PoolLayout<BlockClass<32, 0>, BlockClass<64, 1>>> Pool;
    size_t arena = Pool::ARENA_BYTES;
    EXPECT_EQ(arena, 64u);
    void* block = Pool::allocate(8);
    EXPECT_EQ(Pool::blockBytes(block), 64u);
    EXPECT_EQ(Pool::allocate(8), nullptr);
    EXPECT_EQ(Pool::usage(1).failures, 1u);
    Pool::release(block);
}
