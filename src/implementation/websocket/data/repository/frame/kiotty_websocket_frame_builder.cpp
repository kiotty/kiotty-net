#include "websocket/data/repository/frame/kiotty_websocket_frame_builder.h"

#include "core/kiotty_bytes.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    namespace
    {
        FrameHead headOf(FrameOpcode opcode, uint64_t length, uint8_t mask_bit);
        void      appendBigEndian(FrameHead& head, uint64_t value, size_t bytes);
        uint8_t   lengthCodeOf(uint64_t length);
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

    Span<const char> headBytesOf(const FrameHead& head)
    {
        return Span<const char>{ head.bytes, head.length };
    }

    namespace
    {
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
