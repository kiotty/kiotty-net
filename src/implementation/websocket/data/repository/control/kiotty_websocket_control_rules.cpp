#include "websocket/data/repository/control/kiotty_websocket_control_rules.h"

namespace kiotty::net::implementation
{
    ControlAction controlActionOf(FrameOpcode opcode, bool is_closing)
    {
        if (opcode == FrameOpcode::CLOSE) { return ControlAction::ANSWER_CLOSE; }
        if (opcode == FrameOpcode::PONG) { return ControlAction::CLEAR_PING; }
        const bool is_ping = opcode == FrameOpcode::PING;
        if (is_ping && !is_closing) { return ControlAction::SEND_PONG; }
        return ControlAction::NOTHING;
    }

    TimeoutAction timeoutActionOf(bool is_closing, uint64_t ping_deadline)
    {
        if (is_closing) { return TimeoutAction::CLOSE; }
        if (ping_deadline != 0) { return TimeoutAction::SEND_CLOSE; }
        return TimeoutAction::SEND_PING;
    }
}
