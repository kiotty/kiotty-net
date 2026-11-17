#pragma once

#include "kiotty_net_websocket.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t FRAME_START_BYTES = 2;
    inline constexpr size_t MASK_KEY_BYTES = 4;
    inline constexpr size_t MAX_FRAME_HEAD_BYTES = 14;
    inline constexpr size_t MAX_CONTROL_PAYLOAD_BYTES = 125;

    inline constexpr uint8_t  FINAL_BIT = 0x80;
    inline constexpr uint8_t  MASK_BIT = 0x80;
    inline constexpr uint8_t  SHORT_LENGTH_CODE = 126;
    inline constexpr uint8_t  LONG_LENGTH_CODE = 127;
    inline constexpr size_t   SHORT_LENGTH_BYTES = 2;
    inline constexpr size_t   LONG_LENGTH_BYTES = 8;
    inline constexpr uint64_t SHORT_LENGTH_MIN = 126;
    inline constexpr uint64_t LONG_LENGTH_MIN = 0x10000;

    inline constexpr KiottyNetWebSocketStatus CLOSE_PROTOCOL =
        KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR;

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
    };
}
