#pragma once

#include "context/kiotty_run_gate.h"

#include <atomic>

namespace kiotty::net::implementation
{
    class RunLifetime
    {
    public:
        RunLifetime(RunGate& gate, std::atomic<bool>& is_abandoned);

        bool isRunner() const;
        void abandon();
        bool isReleasable() const;
        void join();

    private:
        RunGate&           _gate;
        std::atomic<bool>& _is_abandoned;
    };
}
