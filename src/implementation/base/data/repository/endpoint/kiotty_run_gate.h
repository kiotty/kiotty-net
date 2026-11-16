#pragma once

#include "core/kiotty_parker.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace kiotty::net::implementation
{
    enum class RunPhase : uint8_t
    {
        IDLE,
        RUNNING,
        FINISHED
    };

    class RunGate
    {
    public:
        RunGate();
        RunGate(const RunGate&) = delete;
        RunGate& operator=(const RunGate&) = delete;

        bool            enter();
        void            start();
        void            finish(KiottyNetCode code);
        KiottyNetCode   await(uint64_t timeout_milliseconds);
        void            join();
        void            clear();
        std::thread::id runner() const;

    private:
        Parker                       _parker;
        std::atomic<bool>            _is_running;
        std::atomic<std::thread::id> _runner;
        KiottyNetCode                _code;
        RunPhase                     _phase;
    };
}
