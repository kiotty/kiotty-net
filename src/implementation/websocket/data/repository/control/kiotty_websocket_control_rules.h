#pragma once

#include "websocket/domain/entity/kiotty_websocket_frame.h"
#include "websocket/domain/entity/kiotty_websocket_frame_step.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    ControlAction controlActionOf(FrameOpcode opcode, bool is_closing);
    TimeoutAction timeoutActionOf(bool is_closing, uint64_t ping_deadline);
}
