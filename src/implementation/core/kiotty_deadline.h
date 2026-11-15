#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    class Deadline
    {
    public:
        explicit Deadline(uint32_t timeout_milliseconds);

        bool     limited() const;
        bool     expired() const;
        uint32_t remaining() const;
        uint32_t bound(uint32_t timeout_milliseconds) const;

    private:
        uint64_t _end;
        bool     _limited;
    };
}
