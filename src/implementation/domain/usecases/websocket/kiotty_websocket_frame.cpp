#include "domain/usecases/websocket/kiotty_websocket_frame.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint8_t  FINAL_BIT = 0x80;
        constexpr uint8_t  RESERVED_BITS = 0x70;
        constexpr uint8_t  OPCODE_BITS = 0x0F;
        constexpr uint8_t  CONTROL_BIT = 0x08;
        constexpr uint8_t  KNOWN_OPCODES[] = { 0x0, 0x1, 0x2, 0x8, 0x9, 0xA };
        constexpr uint8_t  MASK_BIT = 0x80;
        constexpr uint8_t  LENGTH_BITS = 0x7F;
        constexpr uint8_t  SHORT_LENGTH_CODE = 126;
        constexpr uint8_t  LONG_LENGTH_CODE = 127;
        constexpr size_t   SHORT_LENGTH_BYTES = 2;
        constexpr size_t   LONG_LENGTH_BYTES = 8;
        constexpr uint64_t SHORT_LENGTH_MIN = 126;
        constexpr uint64_t LONG_LENGTH_MIN = 0x10000;
        constexpr uint64_t LONG_LENGTH_TOP_BIT = 0x8000000000000000ull;
        constexpr uint32_t BITS_PER_BYTE = 8;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status   PROTOCOL_ERROR = KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR;

        bool      isKnownOpcode(uint8_t opcode);
        uint64_t  bigEndianOf(Span<const char> bytes);
        FrameHead headOf(FrameOpcode opcode, uint64_t length, uint8_t mask_bit);
        void      appendBigEndian(FrameHead& head, uint64_t value, size_t bytes);
        uint8_t   lengthCodeOf(uint64_t length);
    }

    Span<const char> FrameHead::span() const
    {
        return Span<const char>{ bytes, length };
    }

    FrameStartResult parseFrameStart(Span<const char> bytes)
    {
        if (bytes.length != FRAME_START_BYTES) { return FrameStartResult::failure(PROTOCOL_ERROR); }
        const uint8_t first = static_cast<uint8_t>(bytes[0]);
        const uint8_t second = static_cast<uint8_t>(bytes[1]);
        const uint8_t opcode = first & OPCODE_BITS;
        const bool is_reserved = (first & RESERVED_BITS) != 0;
        const bool is_known = isKnownOpcode(opcode);
        if (is_reserved || !is_known) { return FrameStartResult::failure(PROTOCOL_ERROR); }
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
        if (is_bad_control) { return FrameStartResult::failure(PROTOCOL_ERROR); }
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
        if (!is_short && !is_long) { return FrameLengthResult::failure(PROTOCOL_ERROR); }
        const uint64_t length = bigEndianOf(bytes);
        const uint64_t minimum = is_short ? SHORT_LENGTH_MIN : LONG_LENGTH_MIN;
        const bool is_top_set = (length & LONG_LENGTH_TOP_BIT) != 0;
        if (length < minimum || is_top_set) { return FrameLengthResult::failure(PROTOCOL_ERROR); }
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

    FrameHead buildFrameHead(FrameOpcode opcode, uint64_t length)
    {
        return headOf(opcode, length, 0);
    }

    FrameHead buildMaskedFrameHead(FrameOpcode opcode, uint64_t length, const MaskKey& key)
    {
        FrameHead head = headOf(opcode, length, MASK_BIT);
        for (size_t i = 0; i < MASK_KEY_BYTES; ++i)
        {
            head.bytes[head.length + i] = static_cast<char>(key.bytes[i]);
        }
        head.length += MASK_KEY_BYTES;
        return head;
    }

    void applyMask(Span<char> data, const MaskKey& key, size_t offset)
    {
        for (size_t i = 0; i < data.length; ++i)
        {
            const uint8_t mask = key.bytes[(offset + i) % MASK_KEY_BYTES];
            data[i] = static_cast<char>(static_cast<uint8_t>(data[i]) ^ mask);
        }
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

        FrameHead headOf(FrameOpcode opcode, uint64_t length, uint8_t mask_bit)
        {
            FrameHead head{};
            head.bytes[0] = static_cast<char>(FINAL_BIT | static_cast<uint8_t>(opcode));
            head.length = FRAME_START_BYTES;
            const bool is_small = length < SHORT_LENGTH_MIN;
            const bool is_short = !is_small && length < LONG_LENGTH_MIN;
            const uint8_t code = lengthCodeOf(length);
            head.bytes[1] = static_cast<char>(mask_bit | code);
            if (is_short) { appendBigEndian(head, length, SHORT_LENGTH_BYTES); }
            const bool is_long = !is_small && !is_short;
            if (is_long) { appendBigEndian(head, length, LONG_LENGTH_BYTES); }
            return head;
        }

        uint8_t lengthCodeOf(uint64_t length)
        {
            if (length < SHORT_LENGTH_MIN) { return static_cast<uint8_t>(length); }
            if (length < LONG_LENGTH_MIN) { return SHORT_LENGTH_CODE; }
            return LONG_LENGTH_CODE;
        }

        void appendBigEndian(FrameHead& head, uint64_t value, size_t bytes)
        {
            for (size_t i = 0; i < bytes; ++i)
            {
                const uint64_t shift = (bytes - 1 - i) * BITS_PER_BYTE;
                const uint8_t byte = static_cast<uint8_t>(value >> shift);
                head.bytes[head.length + i] = static_cast<char>(byte);
            }
            head.length += bytes;
        }
    }
}
