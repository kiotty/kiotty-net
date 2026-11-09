#include "core/kiotty_parse_integer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

using kiotty::net::implementation::parseHexadecimal;
using kiotty::net::implementation::parseUnsigned;
using kiotty::net::implementation::Span;
using kiotty::net::implementation::UnsignedResult;

namespace
{
    const uint64_t MAX_VALUE = std::numeric_limits<uint64_t>::max();

    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    bool isRejected(UnsignedResult parsed)
    {
        return parsed.code == KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }
}

TEST(ParseUnsigned, EmptyInputIsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseUnsigned(Span<const char>{ nullptr, 0 })));
}

TEST(ParseUnsigned, MaxLengthOfTwentyDigitsIsAccepted)
{
    const UnsignedResult parsed = parseUnsigned(spanOf("18446744073709551615"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value, MAX_VALUE);
    const UnsignedResult padded = parseUnsigned(spanOf("00000000000000000042"));
    ASSERT_TRUE(padded);
    EXPECT_EQ(padded.value, 42u);
}

TEST(ParseUnsigned, MaxLengthPlusOneIsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("000000000000000000001"))));
}

TEST(ParseUnsigned, OverflowIsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("18446744073709551616"))));
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("99999999999999999999"))));
}

TEST(ParseUnsigned, MiddleNullByteIsInvalidArgument)
{
    const std::string text("12\0" "34", 5);
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf(text))));
}

TEST(ParseUnsigned, InvalidUtf8IsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("\xFF" "1"))));
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("1\xC3"))));
}

TEST(ParseUnsigned, SignsSpacesAndLettersAreInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("+1"))));
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("-1"))));
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf(" 1"))));
    EXPECT_TRUE(isRejected(parseUnsigned(spanOf("1a"))));
    const UnsignedResult parsed = parseUnsigned(spanOf("1024"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value, 1024u);
}

TEST(ParseHexadecimal, EmptyInputIsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseHexadecimal(Span<const char>{ nullptr, 0 })));
}

TEST(ParseHexadecimal, MaxLengthOfSixteenDigitsIsAccepted)
{
    const UnsignedResult parsed = parseHexadecimal(spanOf("FFFFFFFFFFFFFFFF"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value, MAX_VALUE);
}

TEST(ParseHexadecimal, MaxLengthPlusOneIsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseHexadecimal(spanOf("00000000000000001"))));
}

TEST(ParseHexadecimal, MiddleNullByteIsInvalidArgument)
{
    const std::string text("a\0" "b", 3);
    EXPECT_TRUE(isRejected(parseHexadecimal(spanOf(text))));
}

TEST(ParseHexadecimal, InvalidUtf8IsInvalidArgument)
{
    EXPECT_TRUE(isRejected(parseHexadecimal(spanOf("\xFF"))));
    EXPECT_TRUE(isRejected(parseHexadecimal(spanOf("a\xE2\x82"))));
}

TEST(ParseHexadecimal, MixedCaseDigitsAndRejectedPrefix)
{
    const UnsignedResult parsed = parseHexadecimal(spanOf("aBcD09"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value, 0xABCD09u);
    EXPECT_TRUE(isRejected(parseHexadecimal(spanOf("0x10"))));
    EXPECT_TRUE(isRejected(parseHexadecimal(spanOf("g"))));
}
