#pragma once

#include <cstddef>

namespace kiotty::net::implementation
{
    struct ConnectionLimits
    {
        size_t receive;
        size_t send;
        size_t notify;
        size_t send_timeout_milliseconds;
    };

    static_assert(sizeof(ConnectionLimits) == 4 * sizeof(size_t),
                  "ConnectionLimits: unexpected padding");
}
