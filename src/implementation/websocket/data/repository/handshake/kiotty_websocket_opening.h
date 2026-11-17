#pragma once

#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    KiottyNetTransition sendOpening(HttpSession& session, size_t written);
    KiottyNetTransition openSocket(HttpSession& session);
}
