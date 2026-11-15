#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr uint32_t EXPIRED_MILLISECONDS = 1;

    uint64_t monotonicMilliseconds();
}
