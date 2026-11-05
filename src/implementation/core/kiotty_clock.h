#if !defined(KIOTTY_CLOCK_H)
#define KIOTTY_CLOCK_H

#include <cstdint>

namespace kiotty
{
    uint64_t monotonicMs();

    class Deadline
    {
    public:
        explicit Deadline(uint32_t timeout_ms);

        bool     limited() const;
        bool     expired() const;
        uint32_t remaining() const;

    private:
        bool     _limited;
        uint64_t _end;
    };
}

#endif
