#pragma once

#include <atomic>

namespace kiotty::net::implementation
{
    class BusyGuard
    {
    public:
        explicit BusyGuard(std::atomic<bool>& is_busy);
        ~BusyGuard();

        BusyGuard(const BusyGuard&) = delete;
        BusyGuard& operator=(const BusyGuard&) = delete;

        bool isHeld() const;

    private:
        std::atomic<bool>& _is_busy;
        bool               _is_held;
    };

    inline BusyGuard::BusyGuard(std::atomic<bool>& is_busy)
        : _is_busy(is_busy)
        , _is_held(false)
    {
        bool is_idle = false;
        _is_held = _is_busy.compare_exchange_strong(is_idle, true);
    }

    inline BusyGuard::~BusyGuard()
    {
        if (_is_held) { _is_busy.store(false); }
    }

    inline bool BusyGuard::isHeld() const
    {
        return _is_held;
    }
}
