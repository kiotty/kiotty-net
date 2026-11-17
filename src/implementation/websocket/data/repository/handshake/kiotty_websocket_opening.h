#pragma once

#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition onSocketWritten(KiottyNetConnectionContext context,
                                        KiottyNetConnectionHandle connection,
                                        KiottyNetConnectionEvent event);
    KiottyNetTransition openSocket(HttpSession& session);
}
