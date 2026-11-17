#pragma once

#include "websocket/domain/entity/kiotty_websocket_frame.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    enum class WebSocketState : uint8_t
    {
        OPENING,
        FRAME_START,
        FRAME_LENGTH,
        FRAME_MASK,
        FRAME_PAYLOAD
    };

    struct FrameRead
    {
        size_t         length;
        WebSocketState state;
    };

    struct SocketFrame
    {
        uint64_t      remaining;
        size_t        offset;
        MaskKey       key;
        FrameOpcode   opcode;
        FrameFinality finality;
        FrameMasking  masking;
    };

    enum class FrameEnd : uint8_t
    {
        PAYLOAD,
        CONTROL,
        DATA
    };

    struct MessageStep
    {
        FrameOpcode message;
        FrameOpcode delivered;
        bool        is_done;
    };

    enum class ControlAction : uint8_t
    {
        ANSWER_CLOSE,
        SEND_PONG,
        CLEAR_PING,
        NOTHING
    };

    enum class TimeoutAction : uint8_t
    {
        CLOSE,
        SEND_CLOSE,
        SEND_PING
    };
}
