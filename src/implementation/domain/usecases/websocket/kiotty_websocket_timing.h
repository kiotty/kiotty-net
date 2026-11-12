#pragma once

#include "data/repository/http/kiotty_http_session.h"
#include "kiotty_net_websocket.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    bool                    isClosing(const HttpSession& session);
    uint32_t                readTimeoutOf(HttpSession& session);
    uint64_t                deadlineAfter(uint32_t milliseconds);
    KiottyNetWebSocketClose closeOf(const HttpSession& session, KiottyNetCode reason);
}
