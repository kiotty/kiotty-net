#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class StopPolicy : uint8_t
    {
        WHEN_STOPPED,
        WHEN_EMPTY
    };
}
