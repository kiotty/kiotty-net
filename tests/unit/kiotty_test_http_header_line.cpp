#include "domain/usecases/http/kiotty_http_header_line.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using kiotty::net::implementation::HeaderResult;
using kiotty::net::implementation::parseHeaderLine;
using kiotty::net::implementation::Span;

namespace
{
    const size_t MAX_LINE = 64;

    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    Span<const char> spanOf(const char* literal)
    {
        return Span<const char>{ literal, std::strlen(literal) };
    }

    std::string textOf(KiottyNetSpan span)
    {
        return std::string(span.data, span.length);
    }

    KiottyNetHttpStatus statusOf(const std::string& line)
    {
        const HeaderResult parsed = parseHeaderLine(spanOf(line), MAX_LINE);
        return parsed.code;
    }

    std::string lineOfLength(size_t length)
    {
        return "Name: " + std::string(length - 6, 'v');
    }
}

TEST(HttpHeaderLine, EmptyInputIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(""));
}

TEST(HttpHeaderLine, LineOfTheMaximumLengthIsAccepted)
{
    const std::string line = lineOfLength(MAX_LINE);
    const HeaderResult parsed = parseHeaderLine(spanOf(line), MAX_LINE);
    ASSERT_TRUE(parsed);
    EXPECT_EQ("Name", textOf(parsed.value.name));
    EXPECT_EQ(MAX_LINE - 6, parsed.value.value.length);
}

TEST(HttpHeaderLine, LineOneOverTheMaximumIsHeaderFieldsTooLarge)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE, statusOf(lineOfLength(MAX_LINE + 1)));
}

TEST(HttpHeaderLine, MiddleNullByteIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(std::string("Name: a\0b", 9)));
}

TEST(HttpHeaderLine, InvalidUtf8InTheValueIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("Name: \xC3\x28"));
}

TEST(HttpHeaderLine, Utf8ValueIsAccepted)
{
    const HeaderResult parsed = parseHeaderLine(spanOf("X-Patient: \xED\x99\x8D"), MAX_LINE);
    ASSERT_TRUE(parsed);
    EXPECT_EQ("\xED\x99\x8D", textOf(parsed.value.value));
}

TEST(HttpHeaderLine, ValueIsTrimmedOfSpacesAndTabs)
{
    const HeaderResult parsed = parseHeaderLine(spanOf("Mcp-Session-Id: \t abc def \t"), MAX_LINE);
    ASSERT_TRUE(parsed);
    EXPECT_EQ("Mcp-Session-Id", textOf(parsed.value.name));
    EXPECT_EQ("abc def", textOf(parsed.value.value));
}

TEST(HttpHeaderLine, EmptyValueIsAccepted)
{
    const HeaderResult parsed = parseHeaderLine(spanOf("Accept:"), MAX_LINE);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(0u, parsed.value.value.length);
}

TEST(HttpHeaderLine, MalformedNamesAreBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("Name : v"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(": v"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("Na(me: v"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("NoColon"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(" folded: v"));
}

TEST(HttpHeaderLine, ControlCharactersInTheValueAreBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("Name: a\rb"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("Name: a\nb"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("Name: a\x7F"));
}
