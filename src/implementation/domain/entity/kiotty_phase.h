#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class Phase : uint8_t
    {
        IDLE,
        HANDSHAKING,
        READY,
        CLOSING
    };
}
