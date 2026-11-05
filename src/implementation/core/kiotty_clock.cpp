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

    Deadline::Deadline(uint32_t timeout_ms)
        : _limited(timeout_ms != 0)
        , _end(monotonicMs() + timeout_ms)
    {
    }

    bool Deadline::limited() const
    {
        return _limited;
    }

    bool Deadline::expired() const
    {
        if (!_limited)
        {
            return false;
        }
        return monotonicMs() >= _end;
    }

    uint32_t Deadline::remaining() const
    {
        if (!_limited)
        {
            return 0;
        }
        uint64_t now = monotonicMs();
        if (now >= _end)
        {
            return 0;
        }
        return static_cast<uint32_t>(_end - now);
    }
}
