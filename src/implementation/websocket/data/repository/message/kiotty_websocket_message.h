#pragma once

#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition finishData(HttpSession& session);
    bool                isInOrder(const HttpSession& session, FrameOpcode opcode);
    bool                storePiece(HttpSession& session, Span<const char> piece);
}
