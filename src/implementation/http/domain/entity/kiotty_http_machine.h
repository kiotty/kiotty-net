#pragma once

#include "http/data/repository/kiotty_http_site.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetStatefulAppContext httpStatefulOf(HttpSite& site);
}
