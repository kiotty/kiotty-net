#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"

#include "core/kiotty_bytes.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint8_t  RESERVED_BITS = 0x70;
        constexpr uint8_t  OPCODE_BITS = 0x0F;
        constexpr uint8_t  CONTROL_BIT = 0x08;
        constexpr uint8_t  KNOWN_OPCODES[] = { 0x0, 0x1, 0x2, 0x8, 0x9, 0xA };
        constexpr uint8_t  LENGTH_BITS = 0x7F;
        constexpr uint64_t LONG_LENGTH_TOP_BIT = 0x8000000000000000ull;

        bool     isKnownOpcode(uint8_t opcode);
        uint64_t bigEndianOf(Span<const char> bytes);
    }

    FrameStartResult parseFrameStart(Span<const char> bytes)
    {
        if (bytes.length != FRAME_START_BYTES) { return FrameStartResult::failure(CLOSE_PROTOCOL); }
        const uint8_t first = static_cast<uint8_t>(bytes[0]);
        const uint8_t second = static_cast<uint8_t>(bytes[1]);
        const uint8_t opcode = first & OPCODE_BITS;
        const bool is_reserved = (first & RESERVED_BITS) != 0;
        const bool is_known = isKnownOpcode(opcode);
        if (is_reserved || !is_known) { return FrameStartResult::failure(CLOSE_PROTOCOL); }
        const FrameStart start{
            static_cast<FrameOpcode>(opcode),
            (first & FINAL_BIT) != 0 ? FrameFinality::FINAL : FrameFinality::MORE,
            (second & MASK_BIT) != 0 ? FrameMasking::MASKED : FrameMasking::PLAIN,
            static_cast<uint8_t>(second & LENGTH_BITS),
        };
        const bool is_control = isControl(start.opcode);
        const bool is_split = start.finality == FrameFinality::MORE;
        const bool is_long = start.length_code > MAX_CONTROL_PAYLOAD_BYTES;
        const bool is_bad_control = is_control && (is_split || is_long);
        if (is_bad_control) { return FrameStartResult::failure(CLOSE_PROTOCOL); }
        return FrameStartResult::success(start);
    }

    size_t extendedLengthBytesOf(uint8_t length_code)
    {
        if (length_code == SHORT_LENGTH_CODE) { return SHORT_LENGTH_BYTES; }
        if (length_code == LONG_LENGTH_CODE) { return LONG_LENGTH_BYTES; }
        return 0;
    }

    FrameLengthResult parseExtendedLength(Span<const char> bytes)
    {
        const bool is_short = bytes.length == SHORT_LENGTH_BYTES;
        const bool is_long = bytes.length == LONG_LENGTH_BYTES;
        if (!is_short && !is_long) { return FrameLengthResult::failure(CLOSE_PROTOCOL); }
        const uint64_t length = bigEndianOf(bytes);
        const uint64_t minimum = is_short ? SHORT_LENGTH_MIN : LONG_LENGTH_MIN;
        const bool is_top_set = (length & LONG_LENGTH_TOP_BIT) != 0;
        if (length < minimum || is_top_set) { return FrameLengthResult::failure(CLOSE_PROTOCOL); }
        return FrameLengthResult::success(length);
    }

    MaskKey parseMaskKey(Span<const char> bytes)
    {
        MaskKey key{};
        for (size_t i = 0; i < MASK_KEY_BYTES && i < bytes.length; ++i)
        {
            key.bytes[i] = static_cast<uint8_t>(bytes[i]);
        }
        return key;
    }

    bool isControl(FrameOpcode opcode)
    {
        return (static_cast<uint8_t>(opcode) & CONTROL_BIT) != 0;
    }

    namespace
    {
        bool isKnownOpcode(uint8_t opcode)
        {
            for (const uint8_t known : KNOWN_OPCODES)
            {
                if (known == opcode) { return true; }
            }
            return false;
        }

        uint64_t bigEndianOf(Span<const char> bytes)
        {
            uint64_t value = 0;
            for (size_t i = 0; i < bytes.length; ++i)
            {
                value = (value << BITS_PER_BYTE) | static_cast<uint8_t>(bytes[i]);
            }
            return value;
        }
    }
}
