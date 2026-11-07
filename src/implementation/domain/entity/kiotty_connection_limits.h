#pragma once

#include <cstddef>

namespace kiotty::net::implementation
{
    struct ConnectionLimits
    {
        size_t receive;
        size_t send;
        size_t notify;
    };

    static_assert(sizeof(ConnectionLimits) == 3 * sizeof(size_t),
                  "ConnectionLimits: unexpected padding");
}
