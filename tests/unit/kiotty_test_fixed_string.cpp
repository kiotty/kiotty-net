#include "core/kiotty_fixed_string.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using kiotty::net::implementation::FixedString;
using kiotty::net::implementation::Span;

namespace
{
    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    std::string stringOf(Span<const char> text)
    {
        return std::string(text.data, text.length);
    }
}

TEST(FixedString, DefaultIsEmptyAndTerminated)
{
    FixedString<8> text;
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(text.length(), 0u);
    EXPECT_STREQ(text.c_str(), "");
    EXPECT_EQ(text.span().length, 0u);
}

TEST(FixedString, AssignUpToMaxLengthKeepsTheTerminator)
{
    FixedString<8> text;
    EXPECT_EQ(FixedString<8>::MAX_LENGTH, 7u);
    ASSERT_TRUE(text.assign(spanOf("1234567")));
    EXPECT_EQ(text.length(), 7u);
    EXPECT_STREQ(text.c_str(), "1234567");
    EXPECT_EQ(stringOf(text.span()), "1234567");
}

TEST(FixedString, AssignLongerThanMaxLengthFailsAndClears)
{
    FixedString<8> text;
    ASSERT_TRUE(text.assign(spanOf("abc")));
    EXPECT_FALSE(text.assign(spanOf("12345678")));
    EXPECT_TRUE(text.empty());
    EXPECT_STREQ(text.c_str(), "");
}

TEST(FixedString, AssignEmptyClearsThePreviousText)
{
    FixedString<8> text;
    ASSERT_TRUE(text.assign(spanOf("abc")));
    ASSERT_TRUE(text.assign(Span<const char>{ nullptr, 0 }));
    EXPECT_TRUE(text.empty());
    EXPECT_STREQ(text.c_str(), "");
}

TEST(FixedString, AppendFillsUpToMaxLength)
{
    FixedString<8> text;
    ASSERT_TRUE(text.assign(spanOf("abc")));
    ASSERT_TRUE(text.append(spanOf("defg")));
    EXPECT_STREQ(text.c_str(), "abcdefg");
    EXPECT_EQ(text.length(), 7u);
}

TEST(FixedString, AppendThatDoesNotFitFailsAndKeepsTheText)
{
    FixedString<8> text;
    ASSERT_TRUE(text.assign(spanOf("abcd")));
    EXPECT_FALSE(text.append(spanOf("efgh")));
    EXPECT_STREQ(text.c_str(), "abcd");
    EXPECT_EQ(text.length(), 4u);
}

TEST(FixedString, ClearEmptiesTheText)
{
    FixedString<8> text;
    ASSERT_TRUE(text.assign(spanOf("abc")));
    text.clear();
    EXPECT_TRUE(text.empty());
    EXPECT_STREQ(text.c_str(), "");
}
