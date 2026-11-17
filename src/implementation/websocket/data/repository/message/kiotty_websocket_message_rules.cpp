#include "websocket/data/repository/message/kiotty_websocket_message_rules.h"

#include "core/kiotty_text.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"

namespace kiotty::net::implementation
{
    bool isInOrder(FrameOpcode message, FrameOpcode opcode)
    {
        if (isControl(opcode)) { return true; }
        const bool is_in_message = message != FrameOpcode::CONTINUATION;
        const bool is_continuation = opcode == FrameOpcode::CONTINUATION;
        return is_in_message == is_continuation;
    }

    MessageStep messageStepOf(const SocketFrame& frame, FrameOpcode message)
    {
        const bool is_first = frame.opcode != FrameOpcode::CONTINUATION;
        const FrameOpcode current = is_first ? frame.opcode : message;
        const FrameOpcode none = FrameOpcode::CONTINUATION;
        if (frame.finality == FrameFinality::MORE) { return MessageStep{ current, none, false }; }
        return MessageStep{ none, current, true };
    }

    bool isBrokenMessage(FrameOpcode opcode, Span<const char> body)
    {
        const bool is_text = opcode == FrameOpcode::TEXT;
        return is_text && !isUtf8(body);
    }

    KiottyNetWebSocketMessage appMessageOf(FrameOpcode opcode, KiottyNetSpan data)
    {
        const bool is_text = opcode == FrameOpcode::TEXT;
        const KiottyNetWebSocketOpcode kind =
            is_text ? KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT : KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY;
        return KiottyNetWebSocketMessage{ kind, data };
    }
}
