#pragma once

#include <cstddef>

namespace kiotty::net::implementation
{
    static constexpr size_t CONNECTIONS_PER_SERVER_LIMIT = 65536;

    struct MemoryLimits
    {
        static constexpr size_t RECEIVE_CAPACITY = 65536;
        static constexpr size_t SEND_CAPACITY = 16384;
        static constexpr size_t PUSH_CAPACITY = 16384;
        static constexpr bool   TLS = KIOTTY_NET_USE_TLS != 0;
    };
}
