#include "core/pool/kiotty_pool_handle.h"

#include <gtest/gtest.h>

#include <cstdint>

using kiotty::net::implementation::PoolHandle;

namespace
{
    PoolHandle handle(int32_t generation, int32_t index)
    {
        PoolHandle result;
        result.generation = generation;
        result.index = index;
        return result;
    }
}

TEST(PoolHandle, ValueFromValueRoundTripsGenerationAndSlot)
{
    PoolHandle original = handle(42, 1234);
    PoolHandle restored = PoolHandle::fromValue(original.value());
    EXPECT_EQ(restored.generation, 42);
    EXPECT_EQ(restored.index, 1234);

    PoolHandle largest = handle(INT32_MAX, 65535);
    PoolHandle back = PoolHandle::fromValue(largest.value());
    EXPECT_EQ(back.generation, INT32_MAX);
    EXPECT_EQ(back.index, 65535);
}

TEST(PoolHandle, ZeroValueIsTheInvalidHandle)
{
    PoolHandle zero = PoolHandle::fromValue(0);
    EXPECT_EQ(zero.generation, 0);
    EXPECT_EQ(zero.index, 0);
    EXPECT_GT(handle(1, 0).value(), 0);
    EXPECT_GT(handle(INT32_MAX, 65535).value(), 0);
}

TEST(PoolHandle, GenerationChangeChangesValueOfSameSlot)
{
    int64_t before = handle(1, 5).value();
    int64_t after = handle(2, 5).value();
    EXPECT_NE(before, after);
    EXPECT_EQ(PoolHandle::fromValue(before).index, PoolHandle::fromValue(after).index);
}
