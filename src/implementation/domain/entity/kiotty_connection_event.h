#pragma once

#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    KiottyNetConnectionEvent connectedEvent();
    KiottyNetConnectionEvent readEvent(KiottyNetSpan data);
    KiottyNetConnectionEvent writtenEvent(size_t length);
    KiottyNetConnectionEvent workStartedEvent(KiottyNetConnectionStream stream);
    KiottyNetConnectionEvent timeoutEvent();
    KiottyNetConnectionEvent closedEvent(KiottyNetCode reason);
}
