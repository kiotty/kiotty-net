#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    class WebSocketHandler;

    KiottyNetTransition finishData(WebSocketHandler& handler, const UpgradeSession& upgrade);
    bool                storePiece(WebSocketHandler& handler,
                                   const UpgradeSession& upgrade,
                                   Span<const char> piece);
}
