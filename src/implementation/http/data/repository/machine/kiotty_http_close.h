#pragma once

#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

namespace kiotty::net::implementation
{
    KiottyNetTransition closeWith(KiottyNetCode reason);
}
