#pragma once

#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition finishData(HttpSession& session);
    bool                storePiece(HttpSession& session, Span<const char> piece);
}
