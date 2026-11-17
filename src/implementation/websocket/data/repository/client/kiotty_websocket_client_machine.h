#pragma once

#include "kiotty_net_statemachine.h"
#include "websocket/data/state/kiotty_websocket_client_state.h"

namespace kiotty::net::implementation
{
    KiottyNetStatefulAppContext webSocketClientStatefulOf(WebSocketClientState& state);
}
