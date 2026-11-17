#pragma once

#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    class WebSocketHandler;

    KiottyNetTransition openSocket(WebSocketHandler& handler, const UpgradeSession& upgrade);
}
