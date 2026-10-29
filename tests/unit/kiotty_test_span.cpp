#include "core/kiotty_span.h"

#include <gtest/gtest.h>

using kiotty::Span;

TEST(Span, BeginEndCoverExactlyLengthElements)
{
    int values[] = { 1, 2, 3, 4 };
    Span<int> span = { values, 3 };
    int visited = 0;
    int sum = 0;
    for (int value : span)
    {
        ++visited;
        sum += value;
    }
    EXPECT_EQ(visited, 3);
    EXPECT_EQ(sum, 6);
    EXPECT_EQ(span[2], 3);
    EXPECT_FALSE(span.empty());
}

TEST(Span, EmptySpanHasNoElementsAndEmptyIsTrue)
{
    Span<int> span = { nullptr, 0 };
    EXPECT_TRUE(span.empty());
    EXPECT_EQ(span.begin(), span.end());
    int visited = 0;
    for (int value : span)
    {
        visited += value;
    }
    EXPECT_EQ(visited, 0);
}
