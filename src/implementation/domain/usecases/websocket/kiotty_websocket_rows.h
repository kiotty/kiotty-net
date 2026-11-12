#pragma once

#include "data/repository/http/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition onSocketWritten(KiottyNetConnectionContext context,
                                        KiottyNetConnectionHandle connection,
                                        KiottyNetConnectionEvent event);
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
    KiottyNetTransition onSocketTimeout(KiottyNetConnectionContext context,
                                        KiottyNetConnectionHandle connection,
                                        KiottyNetConnectionEvent event);
    KiottyNetTransition onCloseWritten(KiottyNetConnectionContext context,
                                       KiottyNetConnectionHandle connection,
                                       KiottyNetConnectionEvent event);
    void                endSocket(HttpSession& session, KiottyNetCode reason);
}
