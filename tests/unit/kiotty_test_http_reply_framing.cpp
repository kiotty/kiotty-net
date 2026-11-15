#include "domain/usecases/http/kiotty_http_reply_framing.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using kiotty::net::implementation::HttpVersion;
using kiotty::net::implementation::Persistence;
using kiotty::net::implementation::ReplyBodyKind;
using kiotty::net::implementation::ReplyFramingResult;
using kiotty::net::implementation::ReplyPhase;
using kiotty::net::implementation::selectReplyFraming;
using kiotty::net::implementation::Span;
using kiotty::net::implementation::StatusLine;

namespace
{
    struct Field
    {
        std::string name;
        std::string value;
    };

    ReplyFramingResult frameOf(KiottyNetHttpMethod method,
                               StatusLine status_line,
                               const std::vector<Field>& fields)
    {
        std::vector<KiottyNetHttpHeader> headers;
        for (const Field& field : fields)
        {
            const KiottyNetSpan name{ field.name.data(), field.name.size() };
            const KiottyNetSpan value{ field.value.data(), field.value.size() };
            headers.push_back(KiottyNetHttpHeader{ name, value });
        }
        const Span<const KiottyNetHttpHeader> span{ headers.data(), headers.size() };
        return selectReplyFraming(method, status_line, span);
    }

    ReplyFramingResult getOf(int32_t status, const std::vector<Field>& fields)
    {
        return frameOf(KIOTTY_NET_HTTP_METHOD_GET, StatusLine{ status, HttpVersion::V11 }, fields);
    }

    const Field CHUNKED{ "Transfer-Encoding", "chunked" };
    const Field LENGTH_TEN{ "Content-Length", "10" };
}

TEST(HttpReplyFraming, HeadReplyHasNoBodyWhateverItsFraming)
{
    const StatusLine ok{ 200, HttpVersion::V11 };
    const ReplyFramingResult framed = frameOf(KIOTTY_NET_HTTP_METHOD_HEAD, ok, { LENGTH_TEN });
    ASSERT_TRUE(framed);
    EXPECT_EQ(ReplyBodyKind::NONE, framed.value.kind);
    EXPECT_EQ(Persistence::KEEP_ALIVE, framed.value.persistence);
    EXPECT_EQ(ReplyPhase::FINAL, framed.value.phase);
}

TEST(HttpReplyFraming, NoContentAndNotModifiedIgnoreFramingHeaders)
{
    for (const int32_t status : { 204, 304 })
    {
        const ReplyFramingResult framed = getOf(status, { CHUNKED, LENGTH_TEN });
        ASSERT_TRUE(framed);
        EXPECT_EQ(ReplyBodyKind::NONE, framed.value.kind);
        EXPECT_EQ(Persistence::KEEP_ALIVE, framed.value.persistence);
    }
}

TEST(HttpReplyFraming, InterimRepliesHaveNoBodyAndAreMarkedInterim)
{
    for (const int32_t status : { 100, 102, 103, 199 })
    {
        const ReplyFramingResult framed = getOf(status, { LENGTH_TEN });
        ASSERT_TRUE(framed);
        EXPECT_EQ(ReplyBodyKind::NONE, framed.value.kind);
        EXPECT_EQ(ReplyPhase::INTERIM, framed.value.phase);
    }
}

TEST(HttpReplyFraming, SwitchingProtocolsIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(101, {}).code);
}

TEST(HttpReplyFraming, StatusOutsideTheRangeIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(0, {}).code);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(600, {}).code);
}

TEST(HttpReplyFraming, ChunkedReplyIsChunked)
{
    const ReplyFramingResult framed = getOf(200, { CHUNKED });
    ASSERT_TRUE(framed);
    EXPECT_EQ(ReplyBodyKind::CHUNKED, framed.value.kind);
    EXPECT_EQ(Persistence::KEEP_ALIVE, framed.value.persistence);
}

TEST(HttpReplyFraming, ContentLengthReplyCarriesTheLength)
{
    const ReplyFramingResult framed = getOf(404, { LENGTH_TEN });
    ASSERT_TRUE(framed);
    EXPECT_EQ(ReplyBodyKind::LENGTH, framed.value.kind);
    EXPECT_EQ(10u, framed.value.length);
    EXPECT_EQ(ReplyPhase::FINAL, framed.value.phase);
}

TEST(HttpReplyFraming, ReplyWithoutFramingRunsUntilCloseAndIsNotKept)
{
    const ReplyFramingResult framed = getOf(200, { { "Connection", "keep-alive" } });
    ASSERT_TRUE(framed);
    EXPECT_EQ(ReplyBodyKind::UNTIL_CLOSE, framed.value.kind);
    EXPECT_EQ(Persistence::CLOSE, framed.value.persistence);
}

TEST(HttpReplyFraming, ConflictingOrUnknownFramingIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(200, { CHUNKED, LENGTH_TEN }).code);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(200, { { "Transfer-Encoding", "gzip" } }).code);
    const Field other_length{ "Content-Length", "11" };
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(200, { LENGTH_TEN, other_length }).code);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, getOf(200, { { "Content-Length", "-1" } }).code);
}

TEST(HttpReplyFraming, ConnectionCloseEndsKeepAlive)
{
    const ReplyFramingResult framed = getOf(200, { LENGTH_TEN, { "Connection", "close" } });
    ASSERT_TRUE(framed);
    EXPECT_EQ(Persistence::CLOSE, framed.value.persistence);
}

TEST(HttpReplyFraming, Http10ClosesUnlessItAsksForKeepAlive)
{
    const StatusLine old_ok{ 200, HttpVersion::V10 };
    const ReplyFramingResult plain = frameOf(KIOTTY_NET_HTTP_METHOD_GET, old_ok, { LENGTH_TEN });
    ASSERT_TRUE(plain);
    EXPECT_EQ(Persistence::CLOSE, plain.value.persistence);
    const Field keep{ "Connection", "Keep-Alive" };
    const ReplyFramingResult kept = frameOf(KIOTTY_NET_HTTP_METHOD_GET, old_ok, { LENGTH_TEN, keep });
    ASSERT_TRUE(kept);
    EXPECT_EQ(Persistence::KEEP_ALIVE, kept.value.persistence);
}
