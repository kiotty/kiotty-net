#include "domain/entity/kiotty_action.h"

#include <gtest/gtest.h>

namespace net = kiotty::net::implementation;

TEST(Action, ThenHoldCarriesTheTimeout)
{
    KiottyNetConnectionAction action = net::thenHold(250);
    EXPECT_EQ(KIOTTY_NET_ACTION_TYPE_HOLD, action.type);
    EXPECT_EQ(250u, action.timeout_milliseconds);
}

TEST(Action, ThenReadCarriesExactAndTimeout)
{
    KiottyNetConnectionAction action = net::thenRead(16, 100);
    EXPECT_EQ(KIOTTY_NET_ACTION_TYPE_READ, action.type);
    EXPECT_EQ(16u, action.exact);
    EXPECT_EQ(100u, action.timeout_milliseconds);
}

TEST(Action, ThenReadUntilCarriesDelimiterMaxAndTimeout)
{
    const char delimiter[] = "\r\n";
    KiottyNetSpan span = { delimiter, 2 };
    KiottyNetConnectionAction action = net::thenReadUntil(span, 1024, 300);
    EXPECT_EQ(KIOTTY_NET_ACTION_TYPE_READ_UNTIL, action.type);
    EXPECT_EQ(delimiter, action.delimiter.data);
    EXPECT_EQ(2u, action.delimiter.length);
    EXPECT_EQ(1024u, action.max);
    EXPECT_EQ(300u, action.timeout_milliseconds);
}

TEST(Action, ThenWriteCarriesDataAndTimeout)
{
    const char text[] = "pong";
    KiottyNetSpan span = { text, 4 };
    KiottyNetConnectionAction action = net::thenWrite(span, 50);
    EXPECT_EQ(KIOTTY_NET_ACTION_TYPE_WRITE, action.type);
    EXPECT_EQ(text, action.data.data);
    EXPECT_EQ(4u, action.data.length);
    EXPECT_EQ(50u, action.timeout_milliseconds);
}

TEST(Action, ThenWorkHasNoTimeout)
{
    KiottyNetConnectionAction action = net::thenWork();
    EXPECT_EQ(KIOTTY_NET_ACTION_TYPE_START_WORK, action.type);
    EXPECT_EQ(0u, action.timeout_milliseconds);
}

TEST(Action, ThenCloseCarriesTheReason)
{
    KiottyNetConnectionAction action = net::thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR);
    EXPECT_EQ(KIOTTY_NET_ACTION_TYPE_CLOSE, action.type);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, action.reason);
    EXPECT_EQ(0u, action.timeout_milliseconds);
}

TEST(Action, AcceptKeepsTheContextAndIgnoreDropsIt)
{
    int marker = 0;
    KiottyNetConnectionDecision accepted = net::acceptConnection(&marker);
    EXPECT_TRUE(accepted.accepted);
    EXPECT_EQ(&marker, accepted.connection_context);
    KiottyNetConnectionDecision ignored = net::ignoreConnection();
    EXPECT_FALSE(ignored.accepted);
    EXPECT_EQ(nullptr, ignored.connection_context);
}
