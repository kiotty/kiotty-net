#pragma once

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    KiottyNetConnectionAction thenHold(uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenRead(size_t exact, uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenReadUntil(KiottyNetSpan delimiter,
                                            size_t max,
                                            uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenWrite(KiottyNetSpan data, uint32_t timeout_milliseconds);
    KiottyNetConnectionAction thenWork();
    KiottyNetConnectionAction thenClose(KiottyNetCode reason);

    KiottyNetConnectionDecision acceptConnection(KiottyNetConnectionContext connection_context);
    KiottyNetConnectionDecision ignoreConnection();
}
