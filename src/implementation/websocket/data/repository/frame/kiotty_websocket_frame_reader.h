#pragma once

#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"
#include "websocket/domain/entity/kiotty_websocket_frame_step.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition stepFrame(HttpSession& session, KiottyNetConnectionEvent event);
    KiottyNetTransition readNext(HttpSession& session, FrameRead read);
    KiottyNetTransition nextFrame(HttpSession& session);
}
