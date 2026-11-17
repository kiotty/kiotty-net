#pragma once

#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "http/data/state/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    UpgradeSession      upgradeSessionOf(HttpSession& session);
    KiottyNetTransition dispatchUpgrade(HttpSession& session,
                                        HttpUpgradeHandler& upgrade,
                                        int32_t route);
    KiottyNetTransition stepUpgraded(HttpSession& session, KiottyNetConnectionEvent event);
}
