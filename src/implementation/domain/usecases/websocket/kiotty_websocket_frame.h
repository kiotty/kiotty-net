#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t FRAME_START_BYTES = 2;
    inline constexpr size_t MASK_KEY_BYTES = 4;
    inline constexpr size_t MAX_FRAME_HEAD_BYTES = 14;
    inline constexpr size_t MAX_CONTROL_PAYLOAD_BYTES = 125;

    enum class FrameOpcode : uint8_t
    {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };

    enum class FrameFinality : uint8_t
    {
        MORE,
        FINAL
    };

    enum class FrameMasking : uint8_t
    {
        PLAIN,
        MASKED
    };

    struct FrameStart
    {
        FrameOpcode   opcode;
        FrameFinality finality;
        FrameMasking  masking;
        uint8_t       length_code;
    };

    struct MaskKey
    {
        uint8_t bytes[MASK_KEY_BYTES];
    };

    struct FrameHead
    {
        char   bytes[MAX_FRAME_HEAD_BYTES];
        size_t length;

        Span<const char> span() const;
    };

    using FrameStartResult = Result<FrameStart, KiottyNetWebSocketStatus>;
    using FrameLengthResult = Result<uint64_t, KiottyNetWebSocketStatus>;

    FrameStartResult  parseFrameStart(Span<const char> bytes);
    size_t            extendedLengthBytesOf(uint8_t length_code);
    FrameLengthResult parseExtendedLength(Span<const char> bytes);
    MaskKey           parseMaskKey(Span<const char> bytes);
    bool              isControl(FrameOpcode opcode);
    FrameHead         buildFrameHead(FrameOpcode opcode, uint64_t length);
    FrameHead         buildMaskedFrameHead(FrameOpcode opcode, uint64_t length, const MaskKey& key);
    void              applyMask(Span<char> data, const MaskKey& key, size_t offset);
    MaskKey           nextMaskKey(std::atomic<uint64_t>& state);
}
