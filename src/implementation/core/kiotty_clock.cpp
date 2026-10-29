#include "core/kiotty_clock.h"

#include <chrono>

namespace kiotty
{
    uint64_t monotonicMs()
    {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        return static_cast<uint64_t>(elapsed.count());
    }
}
