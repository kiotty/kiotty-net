#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/kiotty_http_session.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/kiotty_websocket_socket.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition      answerUpgrade(HttpSession& session, const WebSocketTarget& target);
    KiottyNetWebSocketHandle socketHandleOf(const HttpSession& session);
    Span<const char>         headerOf(const KiottyNetHttpRequest& request, Span<const char> name);
    KiottyNetCode            chooseProtocol(HttpSession& session, Span<const char> protocol);
    bool                     isUpgradeResponse(const KiottyNetHttpRequest& response,
                                               Span<const char> accept,
                                               Span<const char> offered);
}
