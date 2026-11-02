#include "base/context/kiotty_context.h"
#include "base/pool/kiotty_memory_layout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

using kiotty::PoolClass;
using kiotty::net::Context;
using kiotty::net::ContextPoolLayout;

namespace
{
    bool smallerBlock(const PoolClass& left, const PoolClass& right)
    {
        return left.block_bytes < right.block_bytes;
    }
}

TEST(Context, ReservesTheWholeWorstCaseLayoutWhenItIsCreated)
{
    std::unique_ptr<Context> context(new Context());
    ASSERT_TRUE(context->valid());
    std::vector<PoolClass> classes(ContextPoolLayout::classes(), ContextPoolLayout::classes() + ContextPoolLayout::CLASS_COUNT);
    std::sort(classes.begin(), classes.end(), smallerBlock);
    size_t handed_out = 0;
    for (size_t i = 0; i < classes.size(); ++i)
    {
        for (size_t block = 0; block < classes[i].block_count; ++block)
        {
            void* taken = context->pool().allocate(classes[i].block_bytes);
            ASSERT_NE(taken, nullptr) << classes[i].block_bytes << " #" << block;
            handed_out += 1;
        }
    }
    EXPECT_GT(handed_out, 0u);
    EXPECT_EQ(context->pool().allocate(1), nullptr);
    EXPECT_EQ(context->pool().failureCount(), 1u);
}

TEST(Context, EachContextHasItsOwnPool)
{
    Context first;
    Context second;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    void* block = first.pool().allocate(64);
    ASSERT_NE(block, nullptr);
    EXPECT_FALSE(second.pool().release(block));
    EXPECT_TRUE(first.pool().release(block));
}

TEST(Context, LeavesOpenSslAloneWhenAnotherAllocatorIsAlreadyInstalled)
{
    Context context;
    EXPECT_FALSE(context.tlsPooled());
}
