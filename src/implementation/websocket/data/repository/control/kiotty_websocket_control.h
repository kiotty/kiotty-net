#pragma once

#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_websocket.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition onSocketTimeout(KiottyNetConnectionContext context,
                                        KiottyNetConnectionHandle connection,
                                        KiottyNetConnectionEvent event);
    void                endSocket(HttpSession& session, KiottyNetCode reason);
    KiottyNetTransition finishControl(HttpSession& session);
    KiottyNetTransition failSocket(HttpSession& session, KiottyNetWebSocketStatus failure);
    void                storeControl(HttpSession& session, Span<const char> piece);
}
