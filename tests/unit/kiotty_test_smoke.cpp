#include <gtest/gtest.h>

#include "kiotty_net_statemachine.h"

TEST(Build, GoogleTestLinksAndRuns)
{
    EXPECT_EQ(1 + 1, 2);
}

TEST(Build, LibraryLinksAndStatemachineHelperFillsTheStruct)
{
    KiottyNetConnectionAction action = {};
    action.type = KIOTTY_ACTION_READ;
    action.as.read.exact = 4;
    KiottyNetTransition transition = KiottyNet_moveToNextState(2, action);
    EXPECT_EQ(transition.next, 2);
    EXPECT_EQ(transition.entry_action.type, KIOTTY_ACTION_READ);
    EXPECT_EQ(transition.entry_action.as.read.exact, static_cast<size_t>(4));
}
