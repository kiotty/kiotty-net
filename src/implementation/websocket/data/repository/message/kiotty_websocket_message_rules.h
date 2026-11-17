#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"
#include "websocket/domain/entity/kiotty_websocket_frame_step.h"

namespace kiotty::net::implementation
{
    bool                      isInOrder(FrameOpcode message, FrameOpcode opcode);
    MessageStep               messageStepOf(const SocketFrame& frame, FrameOpcode message);
    bool                      isBrokenMessage(FrameOpcode opcode, Span<const char> body);
    KiottyNetWebSocketMessage appMessageOf(FrameOpcode opcode, KiottyNetSpan data);
}
