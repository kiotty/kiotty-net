#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    using SocketHandle = intptr_t;

    static constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
}
