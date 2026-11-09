#include "context/kiotty_run_lifetime.h"

#include <thread>

namespace kiotty::net::implementation
{
    RunLifetime::RunLifetime(RunGate& gate, std::atomic<bool>& is_abandoned)
        : _gate(gate)
        , _is_abandoned(is_abandoned)
    {
    }

    bool RunLifetime::isRunner() const
    {
        return _gate.runner() == std::this_thread::get_id();
    }

    void RunLifetime::abandon()
    {
        _is_abandoned.store(true);
    }

    bool RunLifetime::isReleasable() const
    {
        return _is_abandoned.load() && _gate.runner() == std::thread::id();
    }

    void RunLifetime::join()
    {
        _gate.join();
    }
}
