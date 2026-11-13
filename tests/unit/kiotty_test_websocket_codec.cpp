#include "core/kiotty_base64.h"
#include "core/kiotty_sha1.h"
#include "domain/usecases/websocket/kiotty_websocket_close.h"
#include "domain/usecases/websocket/kiotty_websocket_frame.h"
#include "domain/usecases/websocket/kiotty_websocket_handshake.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace net = kiotty::net::implementation;

using net::FrameFinality;
using net::FrameMasking;
using net::FrameOpcode;
using net::Span;

namespace
{
    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    std::string bytesOf(std::initializer_list<int> values)
    {
        std::string bytes;
        for (int value : values) { bytes.push_back(static_cast<char>(value)); }
        return bytes;
    }

    std::string hexOf(const net::Sha1Digest& digest)
    {
        const char* digits = "0123456789abcdef";
        std::string text;
        for (uint8_t byte : digest.bytes)
        {
            text.push_back(digits[byte >> 4]);
            text.push_back(digits[byte & 0x0F]);
        }
        return text;
    }

    std::string base64Of(const std::string& input)
    {
        std::string output(net::base64LengthOf(input.size()), '\0');
        const Span<const uint8_t> bytes{ reinterpret_cast<const uint8_t*>(input.data()), input.size() };
        const net::EncodedResult encoded = net::encodeBase64(bytes, Span<char>{ &output[0], output.size() });
        return encoded ? output : std::string("<failed>");
    }

    std::string textOf(Span<const char> span)
    {
        return std::string(span.data, span.length);
    }

    const net::MaskKey RFC_KEY = { { 0x37, 0xfa, 0x21, 0x3d } };
}

TEST(Sha1, MatchesTheStandardVectors)
{
    EXPECT_EQ("da39a3ee5e6b4b0d3255bfef95601890afd80709", hexOf(net::sha1Of(spanOf(""), spanOf(""))));
    EXPECT_EQ("a9993e364706816aba3e25717850c26c9cd0d89d", hexOf(net::sha1Of(spanOf("abc"), spanOf(""))));
    EXPECT_EQ("a9993e364706816aba3e25717850c26c9cd0d89d", hexOf(net::sha1Of(spanOf("a"), spanOf("bc"))));
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ("84983e441c3bd26ebaae4aa1f95129e5e54670f1", hexOf(net::sha1Of(spanOf(two_blocks), spanOf(""))));
    const std::string million(1000000, 'a');
    EXPECT_EQ("34aa973cd4c4daa4f61eeb2bdbad27316534016f", hexOf(net::sha1Of(spanOf(million), spanOf(""))));
}

TEST(Base64, MatchesTheStandardVectorsAndChecksText)
{
    EXPECT_EQ("", base64Of(""));
    EXPECT_EQ("Zg==", base64Of("f"));
    EXPECT_EQ("Zm8=", base64Of("fo"));
    EXPECT_EQ("Zm9v", base64Of("foo"));
    EXPECT_EQ("Zm9vYmFy", base64Of("foobar"));
    char small[3] = {};
    const std::string input = "f";
    const Span<const uint8_t> bytes{ reinterpret_cast<const uint8_t*>(input.data()), input.size() };
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, net::encodeBase64(bytes, Span<char>{ small, sizeof(small) }).code);
    EXPECT_TRUE(net::isBase64Text(spanOf("Zm9vYg==")));
    EXPECT_FALSE(net::isBase64Text(spanOf("Zm9vY")));
    EXPECT_FALSE(net::isBase64Text(spanOf("Zm=vYg==")));
    EXPECT_FALSE(net::isBase64Text(spanOf("Zm9v!g==")));
    EXPECT_FALSE(net::isBase64Text(spanOf("Zm9===")));
}

TEST(WebSocketHandshake, AcceptIsTheRfcExample)
{
    EXPECT_EQ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", textOf(net::acceptOf(spanOf("dGhlIHNhbXBsZSBub25jZQ==")).span()));
    EXPECT_TRUE(net::isWebSocketKey(spanOf("dGhlIHNhbXBsZSBub25jZQ==")));
    EXPECT_FALSE(net::isWebSocketKey(spanOf("")));
    EXPECT_FALSE(net::isWebSocketKey(spanOf("dGhlIHNhbXBsZSBub25jZQ=")));
    EXPECT_FALSE(net::isWebSocketKey(spanOf("dGhlIHNhbXBsZSBub25jZQ===")));
    EXPECT_FALSE(net::isWebSocketKey(spanOf("dGhlIHNhbXBsZSBub25jZQAA")));
    EXPECT_FALSE(net::isWebSocketKey(spanOf(std::string("dGhlIHNhbXBs\0SBub25jZQ==", 24))));
}

TEST(WebSocketFrame, ParsesTheRfcExampleStarts)
{
    const net::FrameStartResult text = net::parseFrameStart(spanOf(bytesOf({ 0x81, 0x05 })));
    ASSERT_TRUE(text);
    EXPECT_EQ(FrameOpcode::TEXT, text.value.opcode);
    EXPECT_EQ(FrameFinality::FINAL, text.value.finality);
    EXPECT_EQ(FrameMasking::PLAIN, text.value.masking);
    EXPECT_EQ(5, text.value.length_code);
    const net::FrameStartResult masked = net::parseFrameStart(spanOf(bytesOf({ 0x81, 0x85 })));
    ASSERT_TRUE(masked);
    EXPECT_EQ(FrameMasking::MASKED, masked.value.masking);
    const net::FrameStartResult first = net::parseFrameStart(spanOf(bytesOf({ 0x01, 0x03 })));
    ASSERT_TRUE(first);
    EXPECT_EQ(FrameFinality::MORE, first.value.finality);
    const net::FrameStartResult rest = net::parseFrameStart(spanOf(bytesOf({ 0x80, 0x02 })));
    ASSERT_TRUE(rest);
    EXPECT_EQ(FrameOpcode::CONTINUATION, rest.value.opcode);
    EXPECT_EQ(FrameOpcode::PING, net::parseFrameStart(spanOf(bytesOf({ 0x89, 0x05 }))).value.opcode);
    EXPECT_EQ(FrameOpcode::PONG, net::parseFrameStart(spanOf(bytesOf({ 0x8a, 0x85 }))).value.opcode);
}

TEST(WebSocketFrame, RefusesBrokenStartsWithProtocolError)
{
    const KiottyNetWebSocketStatus error = KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR;
    EXPECT_EQ(error, net::parseFrameStart(spanOf("")).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0x81 }))).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0x81, 0x05, 0x00 }))).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0xC1, 0x05 }))).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0x83, 0x05 }))).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0x8B, 0x00 }))).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0x09, 0x00 }))).code);
    EXPECT_EQ(error, net::parseFrameStart(spanOf(bytesOf({ 0x89, 0x7E }))).code);
}

TEST(WebSocketFrame, ExtendedLengthsAreMinimalAndBelowTheTopBit)
{
    EXPECT_EQ(2u, net::extendedLengthBytesOf(126));
    EXPECT_EQ(8u, net::extendedLengthBytesOf(127));
    EXPECT_EQ(0u, net::extendedLengthBytesOf(125));
    const net::FrameLengthResult two = net::parseExtendedLength(spanOf(bytesOf({ 0x01, 0x00 })));
    ASSERT_TRUE(two);
    EXPECT_EQ(256u, two.value);
    const net::FrameLengthResult eight = net::parseExtendedLength(spanOf(bytesOf({ 0, 0, 0, 0, 0, 1, 0, 0 })));
    ASSERT_TRUE(eight);
    EXPECT_EQ(65536u, eight.value);
    EXPECT_FALSE(net::parseExtendedLength(spanOf(bytesOf({ 0x00, 0x7D }))));
    EXPECT_FALSE(net::parseExtendedLength(spanOf(bytesOf({ 0, 0, 0, 0, 0, 0, 0xFF, 0xFF }))));
    EXPECT_FALSE(net::parseExtendedLength(spanOf(bytesOf({ 0x80, 0, 0, 0, 0, 0, 0, 0 }))));
    EXPECT_FALSE(net::parseExtendedLength(spanOf(bytesOf({ 0x01, 0x00, 0x00 }))));
    EXPECT_FALSE(net::parseExtendedLength(spanOf("")));
}

TEST(WebSocketFrame, BuildsTheRfcExampleHeads)
{
    EXPECT_EQ(bytesOf({ 0x81, 0x05 }), textOf(net::buildFrameHead(FrameOpcode::TEXT, 5).span()));
    EXPECT_EQ(bytesOf({ 0x82, 0x7E, 0x01, 0x00 }), textOf(net::buildFrameHead(FrameOpcode::BINARY, 256).span()));
    EXPECT_EQ(bytesOf({ 0x82, 0x7F, 0, 0, 0, 0, 0, 1, 0, 0 }),
              textOf(net::buildFrameHead(FrameOpcode::BINARY, 65536).span()));
    EXPECT_EQ(bytesOf({ 0x82, 0x7D }), textOf(net::buildFrameHead(FrameOpcode::BINARY, 125).span()));
    EXPECT_EQ(bytesOf({ 0x82, 0x7E, 0xFF, 0xFF }), textOf(net::buildFrameHead(FrameOpcode::BINARY, 65535).span()));
    EXPECT_EQ(bytesOf({ 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d }),
              textOf(net::buildMaskedFrameHead(FrameOpcode::TEXT, 5, RFC_KEY).span()));
}

TEST(WebSocketFrame, MaskingIsTheRfcExampleAndHonoursTheOffset)
{
    std::string hello = "Hello";
    net::applyMask(Span<char>{ &hello[0], hello.size() }, RFC_KEY, 0);
    EXPECT_EQ(bytesOf({ 0x7f, 0x9f, 0x4d, 0x51, 0x58 }), hello);
    std::string first = bytesOf({ 0x7f, 0x9f });
    std::string rest = bytesOf({ 0x4d, 0x51, 0x58 });
    net::applyMask(Span<char>{ &first[0], first.size() }, RFC_KEY, 0);
    net::applyMask(Span<char>{ &rest[0], rest.size() }, RFC_KEY, 2);
    EXPECT_EQ("Hello", first + rest);
    const std::string key_bytes = bytesOf({ 0x37, 0xfa, 0x21, 0x3d });
    EXPECT_EQ(RFC_KEY.bytes[3], net::parseMaskKey(spanOf(key_bytes)).bytes[3]);
}

TEST(WebSocketClose, ParsesStatusAndReason)
{
    const net::ClosePayloadResult empty = net::parseClosePayload(spanOf(""));
    ASSERT_TRUE(empty);
    EXPECT_EQ(1005, empty.value.status);
    const std::string payload = bytesOf({ 0x03, 0xE8 }) + "bye";
    const net::ClosePayloadResult normal = net::parseClosePayload(spanOf(payload));
    ASSERT_TRUE(normal);
    EXPECT_EQ(1000, normal.value.status);
    EXPECT_EQ("bye", textOf(normal.value.reason));
    EXPECT_TRUE(net::parseClosePayload(spanOf(bytesOf({ 0x0F, 0xA0 }))));
    EXPECT_TRUE(net::parseClosePayload(spanOf(bytesOf({ 0x03, 0xE8 }) + std::string("a\0b", 3))));
}

TEST(WebSocketClose, RefusesBrokenPayloads)
{
    const KiottyNetWebSocketStatus error = KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR;
    EXPECT_EQ(error, net::parseClosePayload(spanOf(bytesOf({ 0x03 }))).code);
    EXPECT_EQ(error, net::parseClosePayload(spanOf(bytesOf({ 0x03, 0xED }))).code);
    EXPECT_EQ(error, net::parseClosePayload(spanOf(bytesOf({ 0x03, 0xEE }))).code);
    EXPECT_EQ(error, net::parseClosePayload(spanOf(bytesOf({ 0x03, 0xEC }))).code);
    EXPECT_EQ(error, net::parseClosePayload(spanOf(bytesOf({ 0x00, 0x00 }))).code);
    EXPECT_EQ(error, net::parseClosePayload(spanOf(bytesOf({ 0x13, 0x88 }))).code);
    const std::string longest = bytesOf({ 0x03, 0xE8 }) + std::string(123, 'r');
    EXPECT_TRUE(net::parseClosePayload(spanOf(longest)));
    EXPECT_EQ(error, net::parseClosePayload(spanOf(longest + "r")).code);
    const std::string broken = bytesOf({ 0x03, 0xE8, 0xC3, 0x28 });
    EXPECT_EQ(KIOTTY_NET_WEB_SOCKET_STATUS_INVALID_PAYLOAD, net::parseClosePayload(spanOf(broken)).code);
}

TEST(WebSocketClose, BuildsPayloadsWithinTheControlLimit)
{
    const net::CloseTextResult built = net::buildClosePayload(1000, spanOf("bye"));
    ASSERT_TRUE(built);
    EXPECT_EQ(bytesOf({ 0x03, 0xE8 }) + "bye", textOf(built.value.span()));
    const net::CloseTextResult zero_byte = net::buildClosePayload(4096, spanOf(""));
    ASSERT_TRUE(zero_byte);
    EXPECT_EQ(bytesOf({ 0x10, 0x00 }), textOf(zero_byte.value.span()));
    EXPECT_TRUE(net::buildClosePayload(1000, spanOf(std::string(123, 'r'))));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::buildClosePayload(1000, spanOf(std::string(124, 'r'))).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, net::buildClosePayload(1005, spanOf("")).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, net::buildClosePayload(1000, spanOf(bytesOf({ 0xC3, 0x28 }))).code);
}

TEST(WebSocketFrame, MaskKeysComeFromTheSharedStateAndChangeEveryDraw)
{
    std::atomic<uint64_t> state(42);
    std::atomic<uint64_t> same(42);
    const net::MaskKey first = net::nextMaskKey(state);
    const net::MaskKey second = net::nextMaskKey(state);
    const net::MaskKey again = net::nextMaskKey(same);
    EXPECT_NE(42u, state.load());
    EXPECT_NE(0, std::memcmp(first.bytes, second.bytes, net::MASK_KEY_BYTES));
    EXPECT_EQ(0, std::memcmp(first.bytes, again.bytes, net::MASK_KEY_BYTES));
}
