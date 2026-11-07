#include "core/kiotty_deadline.h"

#include "core/kiotty_clock.h"

namespace kiotty::net::implementation
{
    Deadline::Deadline(uint32_t timeout_milliseconds)
        : _end(monotonicMilliseconds() + timeout_milliseconds)
        , _limited(timeout_milliseconds != 0)
    {
    }

    bool Deadline::limited() const
    {
        return _limited;
    }

    bool Deadline::expired() const
    {
        if (!_limited) { return false; }
        return monotonicMilliseconds() >= _end;
    }

    uint32_t Deadline::remaining() const
    {
        if (!_limited) { return 0; }
        const uint64_t now = monotonicMilliseconds();
        if (now >= _end) { return 0; }
        return static_cast<uint32_t>(_end - now);
    }
}
