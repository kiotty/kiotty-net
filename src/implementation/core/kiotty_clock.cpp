#include "core/kiotty_clock.h"

#include <chrono>

namespace kiotty::net::implementation
{
    uint64_t monotonicMilliseconds()
    {
        using Milliseconds = std::chrono::milliseconds;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const Milliseconds elapsed = std::chrono::duration_cast<Milliseconds>(now.time_since_epoch());
        return static_cast<uint64_t>(elapsed.count());
    }
}
