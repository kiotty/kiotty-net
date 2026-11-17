#pragma once

#include "http/data/repository/server/kiotty_http_site.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetStatefulAppContext webSocketClientStatefulOf(HttpSite& site);
}
