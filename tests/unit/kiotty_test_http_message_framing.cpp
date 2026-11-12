#include "domain/usecases/http/kiotty_http_message_framing.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using kiotty::net::implementation::BodyKind;
using kiotty::net::implementation::decidePersistence;
using kiotty::net::implementation::FramingResult;
using kiotty::net::implementation::HttpVersion;
using kiotty::net::implementation::LengthResult;
using kiotty::net::implementation::parseChunkSize;
using kiotty::net::implementation::parseContentLength;
using kiotty::net::implementation::Persistence;
using kiotty::net::implementation::selectBodyFraming;
using kiotty::net::implementation::Span;

namespace
{
    struct Field
    {
        std::string name;
        std::string value;
    };

    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    Span<const char> spanOf(const char* literal)
    {
        return Span<const char>{ literal, std::strlen(literal) };
    }

    std::vector<KiottyNetHttpHeader> headersOf(const std::vector<Field>& fields)
    {
        std::vector<KiottyNetHttpHeader> headers;
        for (const Field& field : fields)
        {
            const KiottyNetSpan name{ field.name.data(), field.name.size() };
            const KiottyNetSpan value{ field.value.data(), field.value.size() };
            headers.push_back(KiottyNetHttpHeader{ name, value });
        }
        return headers;
    }

    FramingResult frameOf(const std::vector<Field>& fields)
    {
        const std::vector<KiottyNetHttpHeader> headers = headersOf(fields);
        return selectBodyFraming(Span<const KiottyNetHttpHeader>{ headers.data(), headers.size() });
    }

    Persistence persistenceOf(HttpVersion version, const std::vector<Field>& fields)
    {
        const std::vector<KiottyNetHttpHeader> headers = headersOf(fields);
        return decidePersistence(version, Span<const KiottyNetHttpHeader>{ headers.data(), headers.size() });
    }

    KiottyNetHttpStatus lengthStatusOf(const std::string& text)
    {
        const LengthResult parsed = parseContentLength(spanOf(text));
        return parsed.code;
    }

    KiottyNetHttpStatus chunkStatusOf(const std::string& text)
    {
        const LengthResult parsed = parseChunkSize(spanOf(text));
        return parsed.code;
    }
}

TEST(HttpContentLength, EmptyInputIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf(""));
}

TEST(HttpContentLength, TwentyDigitMaximumIsAccepted)
{
    const LengthResult parsed = parseContentLength(spanOf("18446744073709551615"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(18446744073709551615ull, parsed.value);
}

TEST(HttpContentLength, TwentyOneDigitsOrOverflowIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("000000000000000000001"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("18446744073709551616"));
}

TEST(HttpContentLength, MiddleNullByteIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf(std::string("1\0" "2", 3)));
}

TEST(HttpContentLength, InvalidUtf8IsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("1\xC3\x28"));
}

TEST(HttpContentLength, SignsSpacesAndListsAreBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("+5"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("-1"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("5 "));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, lengthStatusOf("5,5"));
}

TEST(HttpChunkSize, EmptyInputIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf(""));
}

TEST(HttpChunkSize, SixteenHexDigitsAreTheMaximum)
{
    const LengthResult parsed = parseChunkSize(spanOf("FFFFFFFFFFFFFFFF"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(18446744073709551615ull, parsed.value);
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf("00000000000000001"));
}

TEST(HttpChunkSize, MiddleNullByteIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf(std::string("1\0" "a", 3)));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf(std::string("1a;x\0", 5)));
}

TEST(HttpChunkSize, InvalidUtf8IsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf("1a;\xC3\x28"));
}

TEST(HttpChunkSize, ExtensionsAfterTheSemicolonAreIgnored)
{
    const LengthResult parsed = parseChunkSize(spanOf("1a;name=value"));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(26u, parsed.value);
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf("1a ;x"));
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, chunkStatusOf(";x"));
}

TEST(HttpBodyFraming, NoFramingHeadersMeansNoBody)
{
    const FramingResult framing = frameOf({ { "Host", "a" } });
    ASSERT_TRUE(framing);
    EXPECT_EQ(BodyKind::NONE, framing.value.kind);
}

TEST(HttpBodyFraming, ContentLengthGivesTheLength)
{
    const FramingResult framing = frameOf({ { "content-length", "42" } });
    ASSERT_TRUE(framing);
    EXPECT_EQ(BodyKind::LENGTH, framing.value.kind);
    EXPECT_EQ(42u, framing.value.length);
}

TEST(HttpBodyFraming, ChunkedTransferEncodingIsAcceptedInAnyCase)
{
    const FramingResult framing = frameOf({ { "Transfer-Encoding", "Chunked" } });
    ASSERT_TRUE(framing);
    EXPECT_EQ(BodyKind::CHUNKED, framing.value.kind);
}

TEST(HttpBodyFraming, OtherTransferCodingsAreNotImplemented)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED, frameOf({ { "Transfer-Encoding", "gzip, chunked" } }).code);
}

TEST(HttpBodyFraming, AmbiguousFramingIsBadRequest)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST,
              frameOf({ { "Transfer-Encoding", "chunked" }, { "Content-Length", "3" } }).code);
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST,
              frameOf({ { "Content-Length", "3" }, { "Content-Length", "3" } }).code);
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST,
              frameOf({ { "Transfer-Encoding", "chunked" }, { "Transfer-Encoding", "chunked" } }).code);
    EXPECT_EQ(KIOTTY_NET_HTTP_STATUS_BAD_REQUEST, frameOf({ { "Content-Length", "x" } }).code);
}

TEST(HttpPersistence, VersionDecidesTheDefault)
{
    EXPECT_EQ(Persistence::KEEP_ALIVE, persistenceOf(HttpVersion::V11, {}));
    EXPECT_EQ(Persistence::CLOSE, persistenceOf(HttpVersion::V10, {}));
}

TEST(HttpPersistence, ConnectionOptionsOverrideTheDefault)
{
    EXPECT_EQ(Persistence::CLOSE, persistenceOf(HttpVersion::V11, { { "Connection", "Close" } }));
    EXPECT_EQ(Persistence::KEEP_ALIVE, persistenceOf(HttpVersion::V10, { { "connection", "keep-alive" } }));
    EXPECT_EQ(Persistence::CLOSE, persistenceOf(HttpVersion::V10, { { "Connection", "Upgrade" } }));
}

TEST(HttpPersistence, CloseWinsOverKeepAliveInAnyList)
{
    EXPECT_EQ(Persistence::CLOSE, persistenceOf(HttpVersion::V11, { { "Connection", "keep-alive , close" } }));
    EXPECT_EQ(Persistence::CLOSE,
              persistenceOf(HttpVersion::V10, { { "Connection", "keep-alive" }, { "Connection", "close" } }));
}
