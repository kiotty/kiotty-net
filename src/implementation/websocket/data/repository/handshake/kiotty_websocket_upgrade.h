#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"

namespace kiotty::net::implementation
{
    class WebSocketHandler;

    UpgradeAnswer            answerUpgrade(WebSocketHandler& handler,
                                           const UpgradeSession& upgrade,
                                           const WebSocketTarget& target);
    KiottyNetWebSocketHandle socketHandleOf(const WebSocketHandler& handler,
                                            const UpgradeSession& upgrade);
    Span<const char>         headerOf(const KiottyNetHttpRequest& request, Span<const char> name);
    Span<const char>         headerOf(Span<const KiottyNetHttpHeader> headers,
                                      Span<const char> name);
    KiottyNetCode            chooseProtocol(WebSocketHandler& handler,
                                            const UpgradeSession& upgrade,
                                            Span<const char> protocol);
    bool                     isUpgradeResponse(Span<const KiottyNetHttpHeader> response,
                                               Span<const char> accept,
                                               Span<const char> offered);
}
