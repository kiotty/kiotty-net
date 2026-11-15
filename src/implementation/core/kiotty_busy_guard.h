#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace kiotty::net::implementation
{
    class BusyFlag
    {
    public:
        BusyFlag();

        BusyFlag(const BusyFlag&) = delete;
        BusyFlag& operator=(const BusyFlag&) = delete;

        bool enterCall();
        bool enterClose();
        void leave();

    private:
        static constexpr uint8_t IDLE = 0;
        static constexpr uint8_t CALL = 1;
        static constexpr uint8_t CLOSE = 2;

        std::atomic<uint8_t> _state;
    };

    class BusyGuard
    {
    public:
        explicit BusyGuard(BusyFlag& flag);
        ~BusyGuard();

        BusyGuard(const BusyGuard&) = delete;
        BusyGuard& operator=(const BusyGuard&) = delete;

        bool isHeld() const;

    private:
        BusyFlag& _flag;
        bool      _is_held;
    };

    inline BusyFlag::BusyFlag()
        : _state(IDLE)
    {
    }

    inline bool BusyFlag::enterCall()
    {
        for (;;)
        {
            uint8_t expected = IDLE;
            if (_state.compare_exchange_strong(expected, CALL)) { return true; }
            if (expected == CALL) { return false; }
            std::this_thread::yield();
        }
    }

    inline bool BusyFlag::enterClose()
    {
        uint8_t expected = IDLE;
        return _state.compare_exchange_strong(expected, CLOSE);
    }

    inline void BusyFlag::leave()
    {
        _state.store(IDLE);
    }

    inline BusyGuard::BusyGuard(BusyFlag& flag)
        : _flag(flag)
        , _is_held(flag.enterCall())
    {
    }

    inline BusyGuard::~BusyGuard()
    {
        if (_is_held) { _flag.leave(); }
    }

    inline bool BusyGuard::isHeld() const
    {
        return _is_held;
    }
}
