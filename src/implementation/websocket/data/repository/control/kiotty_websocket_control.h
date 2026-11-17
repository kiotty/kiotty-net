#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_websocket.h"

namespace kiotty::net::implementation
{
    class WebSocketHandler;

    KiottyNetTransition timeoutSocket(WebSocketHandler& handler, const UpgradeSession& upgrade);
    void                endSocket(WebSocketHandler& handler,
                                  const UpgradeSession& upgrade,
                                  KiottyNetCode reason);
    KiottyNetTransition finishControl(WebSocketHandler& handler, const UpgradeSession& upgrade);
    KiottyNetTransition failSocket(WebSocketHandler& handler,
                                   const UpgradeSession& upgrade,
                                   KiottyNetWebSocketStatus failure);
    void                storeControl(WebSocketHandler& handler,
                                     const UpgradeSession& upgrade,
                                     Span<const char> piece);
}
