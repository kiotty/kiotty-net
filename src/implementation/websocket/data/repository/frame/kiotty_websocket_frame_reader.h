#pragma once

#include "http/data/state/kiotty_http_session.h"
#include "http/domain/entity/kiotty_http_state.h"
#include "kiotty_net_statemachine.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    KiottyNetTransition onFrameStart(KiottyNetConnectionContext context,
                                     KiottyNetConnectionHandle connection,
                                     KiottyNetConnectionEvent event);
    KiottyNetTransition onFrameLength(KiottyNetConnectionContext context,
                                      KiottyNetConnectionHandle connection,
                                      KiottyNetConnectionEvent event);
    KiottyNetTransition onFrameMask(KiottyNetConnectionContext context,
                                    KiottyNetConnectionHandle connection,
                                    KiottyNetConnectionEvent event);
    KiottyNetTransition onFramePayload(KiottyNetConnectionContext context,
                                       KiottyNetConnectionHandle connection,
                                       KiottyNetConnectionEvent event);
    KiottyNetTransition readNext(HttpSession& session, HttpState state, size_t length);
    KiottyNetTransition nextFrame(HttpSession& session);
}
