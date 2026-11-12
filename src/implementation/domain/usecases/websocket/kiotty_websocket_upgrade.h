#pragma once

#include "core/kiotty_span.h"
#include "data/repository/http/kiotty_http_session.h"
#include "data/repository/websocket/kiotty_websocket_socket.h"
#include "kiotty_net_statemachine.h"
#include "kiotty_net_websocket.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition      answerUpgrade(HttpSession& session, const WebSocketTarget& target);
    KiottyNetWebSocketHandle socketHandleOf(const HttpSession& session);
    Span<const char>         headerOf(const KiottyNetHttpRequest& request, Span<const char> name);
    KiottyNetCode            chooseProtocol(HttpSession& session, Span<const char> protocol);
}
