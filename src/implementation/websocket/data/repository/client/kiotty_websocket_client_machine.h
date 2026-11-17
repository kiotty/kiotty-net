#pragma once

#include "http/data/repository/server/kiotty_http_session_host.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetStatefulAppContext webSocketClientStatefulOf(HttpSessionHost& host);
}
