#include "http/data/repository/body/kiotty_http_body_reader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using kiotty::net::implementation::BodyStep;
using kiotty::net::implementation::CHUNK_LINE_BYTES;
using kiotty::net::implementation::HttpBodyReader;
using kiotty::net::implementation::Persistence;
using kiotty::net::implementation::ReplyBodyKind;
using kiotty::net::implementation::ReplyFraming;
using kiotty::net::implementation::ReplyPhase;
using kiotty::net::implementation::Span;

namespace
{
    constexpr size_t WIDE = 4096;

    struct Drained
    {
        std::string   body;
        size_t        consumed;
        KiottyNetCode code;
        bool          is_finished;
    };

    ReplyFraming framingOf(ReplyBodyKind kind, uint64_t length)
    {
        return ReplyFraming{ length, kind, Persistence::KEEP_ALIVE, ReplyPhase::FINAL };
    }

    HttpBodyReader chunkedReader()
    {
        return HttpBodyReader(framingOf(ReplyBodyKind::CHUNKED, 0));
    }

    Drained drain(HttpBodyReader& reader, const std::string& wire, size_t piece_bytes, size_t output_bytes)
    {
        Drained drained{ "", 0, KIOTTY_NET_CODE_SUCCESS, reader.isFinished() };
        std::vector<char> output(output_bytes);
        while (drained.consumed < wire.size())
        {
            const size_t end = std::min(wire.size(), drained.consumed + piece_bytes);
            const Span<const char> input{ wire.data() + drained.consumed, end - drained.consumed };
            const BodyStep step = reader.take(input, Span<char>{ output.data(), output.size() });
            drained.body.append(output.data(), step.produced);
            drained.consumed += step.consumed;
            drained.code = step.code;
            drained.is_finished = step.is_finished;
            const bool is_stuck = step.consumed == 0 && step.produced == 0;
            if (is_stuck || step.code != KIOTTY_NET_CODE_SUCCESS || step.is_finished) { break; }
        }
        return drained;
    }

    Drained drainSplitAt(HttpBodyReader& reader, const std::string& wire, size_t split)
    {
        Drained first = drain(reader, wire.substr(0, split), WIDE, WIDE);
        if (first.code != KIOTTY_NET_CODE_SUCCESS || first.is_finished) { return first; }
        const Drained second = drain(reader, wire.substr(split), WIDE, WIDE);
        return Drained{ first.body + second.body, first.consumed + second.consumed, second.code,
                        second.is_finished };
    }

    KiottyNetCode chunkedCodeOf(const std::string& wire)
    {
        HttpBodyReader reader = chunkedReader();
        return drain(reader, wire, WIDE, WIDE).code;
    }

    const std::string CHUNKED_WIRE = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    const std::string AFTER_BODY = "HTTP/1.1 200 OK\r\n";
}

TEST(HttpBodyReader, DefaultReaderIsFinished)
{
    HttpBodyReader reader;
    EXPECT_TRUE(reader.isFinished());
    char output[4] = {};
    const BodyStep step = reader.take(Span<const char>{ "abc", 3 }, Span<char>{ output, 4 });
    EXPECT_EQ(0u, step.consumed);
    EXPECT_EQ(0u, step.produced);
    EXPECT_TRUE(step.is_finished);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, step.code);
}

TEST(HttpBodyReader, NoneAndZeroLengthAreFinishedImmediately)
{
    EXPECT_TRUE(HttpBodyReader(framingOf(ReplyBodyKind::NONE, 0)).isFinished());
    EXPECT_TRUE(HttpBodyReader(framingOf(ReplyBodyKind::LENGTH, 0)).isFinished());
}

TEST(HttpBodyReader, LengthIsPassedThroughSmallOutputBuffersAndStopsAtTheEnd)
{
    const std::string body = "hello world";
    for (size_t output_bytes = 1; output_bytes <= body.size() + 1; ++output_bytes)
    {
        HttpBodyReader reader(framingOf(ReplyBodyKind::LENGTH, body.size()));
        const Drained drained = drain(reader, body + AFTER_BODY, 4, output_bytes);
        EXPECT_EQ(body, drained.body);
        EXPECT_EQ(body.size(), drained.consumed);
        EXPECT_TRUE(drained.is_finished);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, drained.code);
    }
}

TEST(HttpBodyReader, LengthCutShortByCloseIsPeerClosed)
{
    HttpBodyReader reader(framingOf(ReplyBodyKind::LENGTH, 10));
    const Drained drained = drain(reader, "abc", WIDE, WIDE);
    EXPECT_FALSE(drained.is_finished);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, reader.closeInput());
    char output[4] = {};
    const BodyStep step = reader.take(Span<const char>{ "def", 3 }, Span<char>{ output, 4 });
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, step.code);
    EXPECT_EQ(0u, step.consumed);
    EXPECT_FALSE(reader.isFinished());
}

TEST(HttpBodyReader, UntilCloseIsFinishedOnlyByClose)
{
    HttpBodyReader reader(framingOf(ReplyBodyKind::UNTIL_CLOSE, 0));
    const Drained drained = drain(reader, "event: a\n\ndata: b\n\n", 3, 2);
    EXPECT_EQ("event: a\n\ndata: b\n\n", drained.body);
    EXPECT_FALSE(drained.is_finished);
    EXPECT_FALSE(reader.isFinished());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, reader.closeInput());
    EXPECT_TRUE(reader.isFinished());
}

TEST(HttpBodyReader, CloseAfterAFinishedBodyIsSuccess)
{
    HttpBodyReader reader(framingOf(ReplyBodyKind::LENGTH, 2));
    ASSERT_TRUE(drain(reader, "ab", WIDE, WIDE).is_finished);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, reader.closeInput());
}

TEST(HttpBodyReader, ChunkedBodyIsDecodedAndStopsAtTheEnd)
{
    HttpBodyReader reader = chunkedReader();
    const Drained drained = drain(reader, CHUNKED_WIRE + AFTER_BODY, WIDE, WIDE);
    EXPECT_EQ("hello world", drained.body);
    EXPECT_EQ(CHUNKED_WIRE.size(), drained.consumed);
    EXPECT_TRUE(drained.is_finished);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, drained.code);
}

TEST(HttpBodyReader, ChunkedBodyFedOneByteAtATime)
{
    for (size_t output_bytes = 1; output_bytes <= 3; ++output_bytes)
    {
        HttpBodyReader reader = chunkedReader();
        const Drained drained = drain(reader, CHUNKED_WIRE + AFTER_BODY, 1, output_bytes);
        EXPECT_EQ("hello world", drained.body);
        EXPECT_EQ(CHUNKED_WIRE.size(), drained.consumed);
        EXPECT_TRUE(drained.is_finished);
    }
}

TEST(HttpBodyReader, ChunkedBodySplitAtEveryPosition)
{
    const std::string wire = "a;ext=1\r\n0123456789\r\n1\r\nZ\r\n0\r\nTrailer: x\r\n\r\n";
    for (size_t split = 0; split <= wire.size(); ++split)
    {
        HttpBodyReader reader = chunkedReader();
        const Drained drained = drainSplitAt(reader, wire, split);
        EXPECT_EQ("0123456789Z", drained.body) << "split " << split;
        EXPECT_EQ(wire.size(), drained.consumed) << "split " << split;
        EXPECT_TRUE(drained.is_finished) << "split " << split;
    }
}

TEST(HttpBodyReader, ChunkExtensionsAreIgnored)
{
    HttpBodyReader reader = chunkedReader();
    const Drained drained = drain(reader, "5;name=value;x\r\nhello\r\n0;last=\"y\"\r\n\r\n", 1, 2);
    EXPECT_EQ("hello", drained.body);
    EXPECT_TRUE(drained.is_finished);
}

TEST(HttpBodyReader, TrailersAreSkippedUntilTheEmptyLine)
{
    const std::string wire = "3\r\nabc\r\n0\r\nX-Trailer: 1\r\nOther: 2\r\n\r\n";
    HttpBodyReader reader = chunkedReader();
    const Drained drained = drain(reader, wire + AFTER_BODY, 1, WIDE);
    EXPECT_EQ("abc", drained.body);
    EXPECT_EQ(wire.size(), drained.consumed);
    EXPECT_TRUE(drained.is_finished);
}

TEST(HttpBodyReader, BareLineFeedsAreAccepted)
{
    HttpBodyReader reader = chunkedReader();
    const Drained drained = drain(reader, "3\nabc\n0\nT: 1\n\n", 1, WIDE);
    EXPECT_EQ("abc", drained.body);
    EXPECT_TRUE(drained.is_finished);
}

TEST(HttpBodyReader, AnythingButALineEndAfterChunkDataIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("3\r\nabcX\r\n0\r\n\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("3\r\nabc\rX0\r\n\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("3\r\nabcd\r\n0\r\n\r\n"));
}

TEST(HttpBodyReader, MalformedSizeLinesAreProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("g\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf(" 3\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("-3\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("3\r\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf(std::string("3\0\r\n", 4)));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("3;\x01\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("3;\xC3\x28\r\n"));
}

TEST(HttpBodyReader, HexOverflowIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("10000000000000000\r\n"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, chunkedCodeOf("FFFFFFFFFFFFFFFF\r\nabc"));
}

TEST(HttpBodyReader, SizeLineAtTheLimitIsAcceptedAndOneMoreIsProtocolError)
{
    const std::string extension(CHUNK_LINE_BYTES - 3, 'a');
    HttpBodyReader reader = chunkedReader();
    const Drained drained = drain(reader, "1;" + extension + "\r\nx\r\n0\r\n\r\n", 1, WIDE);
    EXPECT_EQ("x", drained.body);
    EXPECT_TRUE(drained.is_finished);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, chunkedCodeOf("1;" + extension + "a\r\nx\r\n"));
}

TEST(HttpBodyReader, FailureIsSticky)
{
    HttpBodyReader reader = chunkedReader();
    ASSERT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, drain(reader, "z\r\n", WIDE, WIDE).code);
    char output[4] = {};
    const BodyStep step = reader.take(Span<const char>{ "0\r\n\r\n", 5 }, Span<char>{ output, 4 });
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, step.code);
    EXPECT_EQ(0u, step.consumed);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, reader.closeInput());
}

TEST(HttpBodyReader, ChunkedCutShortByCloseIsPeerClosed)
{
    HttpBodyReader reader = chunkedReader();
    const Drained drained = drain(reader, "5\r\nhel", WIDE, WIDE);
    EXPECT_EQ("hel", drained.body);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, reader.closeInput());
}
