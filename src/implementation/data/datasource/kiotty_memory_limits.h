#pragma once

#include "kiotty_memory_limits_configuration.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    static constexpr size_t CONNECTIONS_PER_SERVER_LIMIT = 65536;

    struct MemoryLimits
    {
        static constexpr size_t RECEIVE_CAPACITY = CONFIGURED_RECEIVE_CAPACITY;
        static constexpr size_t SEND_CAPACITY = CONFIGURED_SEND_CAPACITY;
        static constexpr size_t PUSH_CAPACITY = CONFIGURED_PUSH_CAPACITY;
        static constexpr bool   TLS = CONFIGURED_TLS;
    };

    static_assert(MemoryLimits::RECEIVE_CAPACITY > 0, "the receive stage needs capacity");
    static_assert(MemoryLimits::SEND_CAPACITY > 0, "the send stage needs capacity");
    static_assert(MemoryLimits::PUSH_CAPACITY > 0, "the push stage needs capacity");
}
