#include <gtest/gtest.h>

#include "kiotty_net_statemachine.h"

TEST(Build, GoogleTestLinksAndRuns)
{
    EXPECT_EQ(1 + 1, 2);
}

TEST(Build, LibraryLinksAndStatemachineHelperFillsTheStruct)
{
    KiottyNetEntryAction action = KiottyNet_makeReadEntryAction(4);
    EXPECT_EQ(action.type, KIOTTY_ENTRY_ACTION_READ);
    EXPECT_EQ(action.as.read.exact, static_cast<size_t>(4));
}
