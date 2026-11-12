#include "domain/usecases/http/kiotty_http_request_line.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using kiotty::net::implementation::HttpVersion;
using kiotty::net::implementation::parseRequestLine;
using kiotty::net::implementation::RequestLineResult;
using kiotty::net::implementation::Span;

namespace
{
    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    Span<const char> spanOf(const char* literal)
    {
        return Span<const char>{ literal, std::strlen(literal) };
    }

    std::string textOf(Span<const char> span)
    {
        return std::string(span.data, span.length);
    }

    KiottyNetHttpStatus statusOf(const std::string& line)
    {
        const RequestLineResult parsed = parseRequestLine(spanOf(line));
        return parsed.code;
    }

    std::string targetOfLength(size_t length)
    {
        return "/" + std::string(length - 1, 'a');
    }
}

TEST(HttpRequestLine, EmptyInputIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(""));
}

TEST(HttpRequestLine, TargetOfTheMaximumLengthIsAccepted)
{
    const std::string target = targetOfLength(KIOTTY_NET_MAX_URL_LENGTH - 1);
    const std::string line = "GET " + target + " HTTP/1.1";
    const RequestLineResult parsed = parseRequestLine(spanOf(line));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(target, textOf(parsed.value.path));
}

TEST(HttpRequestLine, TargetOneOverTheMaximumIsUriTooLong)
{
    const std::string line = "GET " + targetOfLength(KIOTTY_NET_MAX_URL_LENGTH) + " HTTP/1.1";
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_URI_TOO_LONG, statusOf(line));
}

TEST(HttpRequestLine, MiddleNullByteIsBadRequest)
{
    const std::string line("GET /a\0b HTTP/1.1", 17);
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(line));
}

TEST(HttpRequestLine, InvalidUtf8InTheTargetIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /\xC3\x28 HTTP/1.1"));
}

TEST(HttpRequestLine, SplitsPathAndQueryAtTheFirstQuestionMark)
{
    const RequestLineResult parsed = parseRequestLine(spanOf("POST /mcp?session=1?x HTTP/1.1"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(KIOTTY_NET_HTTP_METHOD_POST, parsed.value.method);
    EXPECT_EQ("/mcp", textOf(parsed.value.path));
    EXPECT_EQ("session=1?x", textOf(parsed.value.query));
    EXPECT_EQ(HttpVersion::V11, parsed.value.version);
}

TEST(HttpRequestLine, PathWithoutQueryHasAnEmptyQuery)
{
    const RequestLineResult parsed = parseRequestLine(spanOf("GET / HTTP/1.0"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ("/", textOf(parsed.value.path));
    EXPECT_EQ(0u, parsed.value.query.length);
    EXPECT_EQ(HttpVersion::V10, parsed.value.version);
}

TEST(HttpRequestLine, EverySupportedMethodIsRecognized)
{
    const char* names[] = { "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS" };
    for (int i = 0; i < KIOTTY_NET_HTTP_METHOD_COUNT; ++i)
    {
        const std::string line = std::string(names[i]) + " /x HTTP/1.1";
        const RequestLineResult parsed = parseRequestLine(spanOf(line));
        ASSERT_TRUE(parsed) << names[i];
        EXPECT_EQ(static_cast<KiottyNetHttpMethod>(i), parsed.value.method);
    }
}

TEST(HttpRequestLine, UnsupportedMethodsAreNotImplemented)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED, statusOf("CONNECT /x HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED, statusOf("TRACE /x HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED, statusOf("get /x HTTP/1.1"));
}

TEST(HttpRequestLine, MethodThatIsNotATokenIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("G(T /x HTTP/1.1"));
}

TEST(HttpRequestLine, AsteriskIsOnlyForOptions)
{
    const RequestLineResult parsed = parseRequestLine(spanOf("OPTIONS * HTTP/1.1"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ("*", textOf(parsed.value.path));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET * HTTP/1.1"));
}

TEST(HttpRequestLine, TargetsOtherThanOriginFormAreBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET http://a/x HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /x#frag HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /\x7F HTTP/1.1"));
}

TEST(HttpRequestLine, VersionsOtherThanOneZeroAndOneOneAreBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /x HTTP/2.0"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /x http/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /x"));
}

TEST(HttpRequestLine, ExtraOrMissingSpacesAreBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET  /x HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /x HTTP/1.1 "));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf(" GET /x HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, statusOf("GET /a b HTTP/1.1"));
}
