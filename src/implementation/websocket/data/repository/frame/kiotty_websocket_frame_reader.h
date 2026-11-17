#pragma once

#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_statemachine.h"
#include "websocket/domain/entity/kiotty_websocket_frame_step.h"

namespace kiotty::net::implementation
{
    class WebSocketHandler;

    KiottyNetTransition stepFrame(WebSocketHandler& handler,
                                  const UpgradeSession& upgrade,
                                  KiottyNetConnectionEvent event);
    KiottyNetTransition readNext(WebSocketHandler& handler,
                                 const UpgradeSession& upgrade,
                                 FrameRead read);
    KiottyNetTransition nextFrame(WebSocketHandler& handler, const UpgradeSession& upgrade);
}
