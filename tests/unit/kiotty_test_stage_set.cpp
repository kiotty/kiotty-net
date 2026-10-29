#include "core/kiotty_stage_set.h"

#include <gtest/gtest.h>

using kiotty::StageSet;

TEST(StageSet, EachMemberHasItsOwnCapacity)
{
    StageSet set(16, 32, 64);
    EXPECT_TRUE(set.valid());
    EXPECT_EQ(set.receive.capacity(), 16u);
    EXPECT_EQ(set.send.capacity(), 32u);
    EXPECT_EQ(set.notify.capacity(), 64u);
}

TEST(StageSet, ZeroMembersAllocateNothingAndSetStaysValid)
{
    StageSet empty(0, 0, 0);
    EXPECT_TRUE(empty.valid());
    EXPECT_EQ(empty.receive.capacity(), 0u);
    EXPECT_EQ(empty.send.capacity(), 0u);
    EXPECT_EQ(empty.notify.capacity(), 0u);

    StageSet receive_only(16, 0, 0);
    EXPECT_TRUE(receive_only.valid());
    EXPECT_EQ(receive_only.receive.capacity(), 16u);
    EXPECT_EQ(receive_only.send.capacity(), 0u);
    EXPECT_EQ(receive_only.notify.capacity(), 0u);
}
