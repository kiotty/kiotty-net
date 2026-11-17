#include "websocket/data/repository/frame/kiotty_websocket_frame_rules.h"

#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"
#include "websocket/data/repository/message/kiotty_websocket_message_rules.h"

namespace kiotty::net::implementation
{
    bool isAcceptedStart(const FrameStart& start, SocketRole role, FrameOpcode message)
    {
        const bool is_masked = start.masking == FrameMasking::MASKED;
        const bool is_server = role == SocketRole::SERVER;
        return is_masked == is_server && isInOrder(message, start.opcode);
    }

    SocketFrame frameOf(const FrameStart& start)
    {
        return SocketFrame{ 0, 0, MaskKey{}, start.opcode, start.finality, start.masking };
    }

    bool isOversized(const SocketFrame& frame, uint64_t room)
    {
        return !isControl(frame.opcode) && frame.remaining > room;
    }

    FrameEnd frameEndOf(const SocketFrame& frame)
    {
        if (frame.remaining > 0) { return FrameEnd::PAYLOAD; }
        if (isControl(frame.opcode)) { return FrameEnd::CONTROL; }
        return FrameEnd::DATA;
    }

    FrameRead payloadReadOf(const SocketFrame& frame, size_t capacity)
    {
        const uint64_t remaining = frame.remaining;
        const uint64_t step = remaining < capacity ? remaining : capacity;
        return FrameRead{ static_cast<size_t>(step), WebSocketState::FRAME_PAYLOAD };
    }

    SocketFrame advanceFrame(const SocketFrame& frame, size_t piece)
    {
        SocketFrame next = frame;
        next.offset += piece;
        next.remaining -= piece;
        return next;
    }
}
