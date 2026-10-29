#include "core/kiotty_pool_handle.h"

#include <gtest/gtest.h>

using kiotty::OwnerIndex;
using kiotty::PoolHandle;

namespace
{
    OwnerIndex owner(uint32_t context_slot, uint32_t owner_slot, uint32_t owner_generation)
    {
        OwnerIndex index;
        index.context_slot = context_slot;
        index.owner_slot = owner_slot;
        index.owner_generation = owner_generation;
        return index;
    }

    PoolHandle handle(int32_t generation, int32_t index)
    {
        PoolHandle result;
        result.generation = generation;
        result.index = index;
        return result;
    }
}

TEST(PoolHandle, ToRawFromRawRoundTripsEveryField)
{
    int32_t index = kiotty::makeIndex(owner(3, 7, 200), 1234);
    PoolHandle original = handle(42, index);
    int64_t raw = kiotty::toRaw(original);
    PoolHandle restored = kiotty::fromRaw(raw);
    EXPECT_EQ(restored.generation, 42);
    EXPECT_EQ(restored.index, index);
    OwnerIndex restored_owner = kiotty::ownerOf(restored.index);
    EXPECT_EQ(restored_owner.context_slot, 3u);
    EXPECT_EQ(restored_owner.owner_slot, 7u);
    EXPECT_EQ(restored_owner.owner_generation, 200u);
    EXPECT_EQ(kiotty::slotOf(restored.index), 1234u);
}

TEST(PoolHandle, ZeroRawIsTheInvalidHandle)
{
    PoolHandle zero = kiotty::fromRaw(0);
    EXPECT_EQ(zero.generation, 0);
    EXPECT_EQ(zero.index, 0);
    int32_t index = kiotty::makeIndex(owner(0, 0, 0), 0);
    PoolHandle first = handle(1, index);
    EXPECT_NE(kiotty::toRaw(first), 0);
}

TEST(PoolHandle, OwnerOfAndSlotOfExtractTheirBits)
{
    int32_t index = kiotty::makeIndex(owner(15, 15, 255), 65535);
    EXPECT_EQ(static_cast<uint32_t>(index), 0xFFFFFFFFu);
    OwnerIndex full = kiotty::ownerOf(index);
    EXPECT_EQ(full.context_slot, 15u);
    EXPECT_EQ(full.owner_slot, 15u);
    EXPECT_EQ(full.owner_generation, 255u);
    EXPECT_EQ(kiotty::slotOf(index), 65535u);

    int32_t context_only = kiotty::makeIndex(owner(1, 0, 0), 0);
    EXPECT_EQ(static_cast<uint32_t>(context_only), 0x10000000u);
    int32_t owner_only = kiotty::makeIndex(owner(0, 1, 0), 0);
    EXPECT_EQ(static_cast<uint32_t>(owner_only), 0x01000000u);
    int32_t generation_only = kiotty::makeIndex(owner(0, 0, 1), 0);
    EXPECT_EQ(static_cast<uint32_t>(generation_only), 0x00010000u);
    int32_t slot_only = kiotty::makeIndex(owner(0, 0, 0), 1);
    EXPECT_EQ(static_cast<uint32_t>(slot_only), 0x00000001u);
}

TEST(PoolHandle, GenerationChangeChangesRawOfSameSlot)
{
    int32_t index = kiotty::makeIndex(owner(2, 3, 4), 5);
    int64_t before = kiotty::toRaw(handle(1, index));
    int64_t after = kiotty::toRaw(handle(2, index));
    EXPECT_NE(before, after);
    EXPECT_EQ(kiotty::fromRaw(before).index, kiotty::fromRaw(after).index);
}
