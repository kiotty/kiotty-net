#include "core/kiotty_stage_set.h"

#include <gtest/gtest.h>

typedef kiotty::StageSet<64, 128, 256> SmallSet;

TEST(StageSet, EachMemberHasItsOwnLimit)
{
    SmallSet set;
    EXPECT_EQ(set.receive.capacity(), 64u);
    EXPECT_EQ(set.send.capacity(), 128u);
    EXPECT_EQ(set.notify.capacity(), 256u);
    set.limit(16, 32, 64);
    EXPECT_EQ(set.receive.capacity(), 16u);
    EXPECT_EQ(set.send.capacity(), 32u);
    EXPECT_EQ(set.notify.capacity(), 64u);
}

TEST(StageSet, ZeroLimitsLeaveEveryMemberAlwaysFull)
{
    SmallSet empty;
    empty.limit(0, 0, 0);
    EXPECT_EQ(empty.receive.provider().writable(), 0u);
    EXPECT_EQ(empty.send.provider().writable(), 0u);
    EXPECT_EQ(empty.notify.provider().writable(), 0u);

    SmallSet receive_only;
    receive_only.limit(16, 0, 0);
    EXPECT_EQ(receive_only.receive.provider().writable(), 16u);
    EXPECT_EQ(receive_only.send.provider().writable(), 0u);
    EXPECT_EQ(receive_only.notify.provider().writable(), 0u);
}

TEST(StageSet, ClearEmptiesEveryMember)
{
    SmallSet set;
    set.receive.provider().commit(3);
    set.send.provider().commit(4);
    set.notify.provider().commit(5);
    set.clear();
    EXPECT_EQ(set.receive.consumer().readable(), 0u);
    EXPECT_EQ(set.send.consumer().readable(), 0u);
    EXPECT_EQ(set.notify.consumer().readable(), 0u);
}
