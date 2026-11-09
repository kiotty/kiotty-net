#include "core/kiotty_text.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

using kiotty::net::implementation::equalsIgnoreCase;
using kiotty::net::implementation::parseText;
using kiotty::net::implementation::Span;
using kiotty::net::implementation::TextResult;
using kiotty::net::implementation::textOf;

namespace
{
    const size_t MAX_LENGTH = 8;

    Span<const char> spanOf(const char* text)
    {
        return Span<const char>{ text, std::char_traits<char>::length(text) };
    }

    bool isRejected(const char* text)
    {
        const TextResult parsed = parseText(text, MAX_LENGTH);
        return parsed.code == KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }
}

TEST(ParseText, EmptyInputIsAnEmptySpan)
{
    const TextResult parsed = parseText("", MAX_LENGTH);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value.length, 0u);
}

TEST(ParseText, MaxLengthIsAccepted)
{
    const char* text = "12345678";
    const TextResult parsed = parseText(text, MAX_LENGTH);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value.data, text);
    EXPECT_EQ(parsed.value.length, MAX_LENGTH);
}

TEST(ParseText, MaxLengthPlusOneIsInvalidArgument)
{
    EXPECT_TRUE(isRejected("123456789"));
}

TEST(ParseText, MiddleNullByteEndsTheTextThereAndIsNotAFailure)
{
    const char text[] = { 'a', 'b', '\0', 'c', 'd', '\0' };
    const TextResult parsed = parseText(text, MAX_LENGTH);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value.length, 2u);
}

TEST(ParseText, InvalidUtf8IsInvalidArgument)
{
    EXPECT_TRUE(isRejected("\xC3\x28"));
    EXPECT_TRUE(isRejected("\xC0\xAF"));
    EXPECT_TRUE(isRejected("\xE2\x82"));
    EXPECT_TRUE(isRejected("\xED\xA0\x80"));
    EXPECT_TRUE(isRejected("\xF4\x90\x80\x80"));
    EXPECT_TRUE(isRejected("\xFF"));
    EXPECT_TRUE(isRejected("a\x80"));
}

TEST(ParseText, ValidMultibyteUtf8IsAccepted)
{
    const TextResult parsed = parseText("\xED\x95\x9C\xF0\x9F\x98\x80", MAX_LENGTH);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value.length, 7u);
}

TEST(ParseText, NullIsInvalidArgument)
{
    EXPECT_TRUE(isRejected(nullptr));
}

TEST(ParseText, MaxLengthThatOverflowsTheScanIsInvalidArgument)
{
    const TextResult parsed = parseText("a", std::numeric_limits<size_t>::max());
    EXPECT_EQ(parsed.code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
}

TEST(EqualsIgnoreCase, FoldsAsciiLettersOnly)
{
    EXPECT_TRUE(equalsIgnoreCase(spanOf("Chunked"), spanOf("cHUNKED")));
    EXPECT_TRUE(equalsIgnoreCase(spanOf(""), spanOf("")));
    EXPECT_FALSE(equalsIgnoreCase(spanOf("chunked"), spanOf("chunke")));
    EXPECT_FALSE(equalsIgnoreCase(spanOf("["), spanOf("{")));
    EXPECT_FALSE(equalsIgnoreCase(spanOf("\xC3\x89"), spanOf("\xC3\xA9")));
}

TEST(TextOf, StopsAtTheTerminatorOrTheFieldEnd)
{
    const char terminated[8] = "abc";
    EXPECT_EQ(textOf(terminated).length, 3u);
    const char full[4] = { 'a', 'b', 'c', 'd' };
    EXPECT_EQ(textOf(full).length, 4u);
}
