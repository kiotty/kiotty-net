#pragma once

#include "websocket/domain/entity/kiotty_websocket_frame.h"
#include "websocket/domain/entity/kiotty_websocket_frame_step.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    bool        isAcceptedStart(const FrameStart& start, SocketRole role, FrameOpcode message);
    SocketFrame frameOf(const FrameStart& start);
    bool        isOversized(const SocketFrame& frame, uint64_t room);
    FrameEnd    frameEndOf(const SocketFrame& frame);
    FrameRead   payloadReadOf(const SocketFrame& frame, size_t capacity);
    SocketFrame advanceFrame(const SocketFrame& frame, size_t piece);
}
