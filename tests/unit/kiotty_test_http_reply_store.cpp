#include "http/data/repository/kiotty_http_reply_store.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

using kiotty::net::implementation::HttpReplyStore;
using kiotty::net::implementation::ReplyBodyKind;
using kiotty::net::implementation::ReplyFramingResult;
using kiotty::net::implementation::ReplyPhase;
using kiotty::net::implementation::Span;

namespace
{
    constexpr size_t HEADER_BYTES = 64;
    constexpr size_t MAX_HEADERS = 3;

    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    std::string textOf(KiottyNetSpan span)
    {
        return std::string(span.data, span.length);
    }

    struct StoreFixture
    {
        std::array<char, HEADER_BYTES>               buffer{};
        std::array<KiottyNetHttpHeader, MAX_HEADERS> headers{};
        HttpReplyStore store{ Span<char>{ buffer.data(), buffer.size() },
                              Span<KiottyNetHttpHeader>{ headers.data(), headers.size() } };

        StoreFixture()
        {
            store.begin();
        }

        KiottyNetCode status(const std::string& line)
        {
            return store.takeStatusLine(spanOf(line));
        }

        KiottyNetCode header(const std::string& line)
        {
            return store.takeHeader(spanOf(line));
        }
    };
}

TEST(HttpReplyStore, KeepsStatusAndHeaders)
{
    StoreFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status("HTTP/1.1 200 OK"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.header("Content-Length: 5"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.header("X-Id:  abc "));
    EXPECT_EQ(200, fixture.store.status());
    const Span<const KiottyNetHttpHeader> headers = fixture.store.headers();
    ASSERT_EQ(2u, headers.length);
    EXPECT_EQ("Content-Length", textOf(headers[0].name));
    EXPECT_EQ("abc", textOf(headers[1].value));
    const ReplyFramingResult framed = fixture.store.finish(KIOTTY_NET_HTTP_METHOD_GET);
    ASSERT_TRUE(framed);
    EXPECT_EQ(ReplyBodyKind::LENGTH, framed.value.kind);
    EXPECT_EQ(5u, framed.value.length);
}

TEST(HttpReplyStore, MalformedLinesAreProtocolError)
{
    StoreFixture fixture;
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, fixture.status("HTTP/1.1 20"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status("HTTP/1.1 200"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, fixture.header("No colon"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, fixture.header(" folded: v"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, fixture.header("Name: \xC3\x28"));
}

TEST(HttpReplyStore, HeadBytesFillTheBufferExactlyThenBufferFull)
{
    StoreFixture fixture;
    const std::string status_line = "HTTP/1.1 200";
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status(status_line));
    const size_t left = HEADER_BYTES - status_line.size();
    const std::string exact = "A: " + std::string(left - 3, 'v');
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.header(exact));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.header("B: 1"));
}

TEST(HttpReplyStore, OneByteOverTheBufferIsBufferFull)
{
    StoreFixture fixture;
    const std::string status_line = "HTTP/1.1 200";
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status(status_line));
    const size_t left = HEADER_BYTES - status_line.size();
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.header("A: " + std::string(left - 2, 'v')));
}

TEST(HttpReplyStore, StatusLineLongerThanTheBufferIsBufferFull)
{
    StoreFixture fixture;
    const std::string line = "HTTP/1.1 200 " + std::string(HEADER_BYTES, 'r');
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.status(line));
}

TEST(HttpReplyStore, HeaderCountOverTheTableIsBufferFull)
{
    StoreFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status("HTTP/1.1 200"));
    for (size_t i = 0; i < MAX_HEADERS; ++i)
    {
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.header("A: 1"));
    }
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.header("A: 1"));
}

TEST(HttpReplyStore, InterimReplyIsSkippedByBeginningAgain)
{
    StoreFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status("HTTP/1.1 100 Continue"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.header("X-Interim: 1"));
    const ReplyFramingResult interim = fixture.store.finish(KIOTTY_NET_HTTP_METHOD_POST);
    ASSERT_TRUE(interim);
    ASSERT_EQ(ReplyPhase::INTERIM, interim.value.phase);
    fixture.store.begin();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status("HTTP/1.1 201 Created"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.header("Transfer-Encoding: chunked"));
    const ReplyFramingResult final_reply = fixture.store.finish(KIOTTY_NET_HTTP_METHOD_POST);
    ASSERT_TRUE(final_reply);
    EXPECT_EQ(ReplyPhase::FINAL, final_reply.value.phase);
    EXPECT_EQ(ReplyBodyKind::CHUNKED, final_reply.value.kind);
    EXPECT_EQ(201, fixture.store.status());
    ASSERT_EQ(1u, fixture.store.headers().length);
    EXPECT_EQ("Transfer-Encoding", textOf(fixture.store.headers()[0].name));
}

TEST(HttpReplyStore, SwitchingProtocolsFailsToFinish)
{
    StoreFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.status("HTTP/1.1 101 Switching Protocols"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, fixture.store.finish(KIOTTY_NET_HTTP_METHOD_GET).code);
}

TEST(HttpReplyStore, FinishWithoutAStatusLineIsProtocolError)
{
    StoreFixture fixture;
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, fixture.store.finish(KIOTTY_NET_HTTP_METHOD_GET).code);
}
