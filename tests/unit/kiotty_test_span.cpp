#include "core/kiotty_span.h"

#include <gtest/gtest.h>

using kiotty::net::implementation::Span;

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

TEST(Span, SkipDropsTheFrontAndStopsAtTheEnd)
{
    int values[] = { 1, 2, 3, 4 };
    const Span<int> span = { values, 4 };
    const Span<int> rest = span.skip(1);
    EXPECT_EQ(rest.data, values + 1);
    EXPECT_EQ(rest.length, 3u);
    const Span<int> none = span.skip(9);
    EXPECT_EQ(none.data, values + 4);
    EXPECT_TRUE(none.empty());
}

TEST(Span, FillWritesEveryElement)
{
    int values[] = { 1, 2, 3, 4 };
    const Span<int> span = { values, 3 };
    span.fill(7);
    EXPECT_EQ(values[0], 7);
    EXPECT_EQ(values[2], 7);
    EXPECT_EQ(values[3], 4);
}

TEST(Span, CopyFromCopiesAShorterOrEqualSource)
{
    const char source[] = { 'a', 'b', 'c' };
    char target[4] = { 'x', 'x', 'x', 'x' };
    const Span<char> destination = { target, 4 };
    EXPECT_TRUE(destination.copyFrom(Span<const char>{ source, 3 }));
    EXPECT_EQ(target[0], 'a');
    EXPECT_EQ(target[2], 'c');
    EXPECT_EQ(target[3], 'x');
}

TEST(Span, CopyFromRefusesASourceLongerThanTheDestination)
{
    const char source[] = { 'a', 'b', 'c' };
    char target[2] = { 'x', 'x' };
    const Span<char> destination = { target, 2 };
    EXPECT_FALSE(destination.copyFrom(Span<const char>{ source, 3 }));
    EXPECT_EQ(target[0], 'x');
    EXPECT_EQ(target[1], 'x');
}

TEST(Span, CopyFromAnEmptySourceTouchesNothing)
{
    char target[1] = { 'x' };
    const Span<char> destination = { target, 1 };
    EXPECT_TRUE(destination.copyFrom(Span<const char>{ nullptr, 0 }));
    EXPECT_EQ(target[0], 'x');
}
