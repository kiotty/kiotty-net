#pragma once

#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_websocket.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class WebSocketHandler;

    bool                    isClosing(WebSocketHandler& handler, const UpgradeSession& upgrade);
    uint32_t                readTimeoutOf(WebSocketHandler& handler, const UpgradeSession& upgrade);
    uint64_t                deadlineAfter(uint32_t milliseconds);
    KiottyNetWebSocketClose closeOf(WebSocketHandler& handler,
                                    const UpgradeSession& upgrade,
                                    KiottyNetCode reason);
}
