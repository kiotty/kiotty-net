#include "context/kiotty_run_gate.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint64_t WAIT_FOREVER = 0;
    }

    RunGate::RunGate()
        : _parker()
        , _is_running(false)
        , _runner()
        , _code(KIOTTY_NET_CODE_SUCCESS)
        , _phase(RunPhase::IDLE)
    {
    }

    bool RunGate::enter()
    {
        const bool was_running = _is_running.exchange(true);
        if (was_running) { return false; }
        _runner.store(std::this_thread::get_id());
        _parker.unpark([this]() { _phase = RunPhase::IDLE; });
        return true;
    }

    void RunGate::start()
    {
        _parker.unpark([this]() { _phase = RunPhase::RUNNING; });
    }

    void RunGate::finish(KiottyNetCode code)
    {
        _parker.unpark([this, code]() {
            _code = code;
            _phase = RunPhase::FINISHED;
            _is_running.store(false);
            _runner.store(std::thread::id());
        });
    }

    KiottyNetCode RunGate::await(uint64_t timeout_milliseconds)
    {
        KiottyNetCode answer = KIOTTY_NET_CODE_TIMEOUT;
        const auto settled = [this, &answer]() {
            if (_phase == RunPhase::IDLE) { return false; }
            answer = _phase == RunPhase::RUNNING ? KIOTTY_NET_CODE_SUCCESS : _code;
            return true;
        };
        (void)_parker.park(settled, timeout_milliseconds);
        return answer;
    }

    void RunGate::join()
    {
        (void)_parker.park([this]() { return !_is_running.load(); }, WAIT_FOREVER);
    }

    void RunGate::clear()
    {
        _parker.unpark([this]() {
            _code = KIOTTY_NET_CODE_SUCCESS;
            _phase = RunPhase::IDLE;
        });
    }

    std::thread::id RunGate::runner() const
    {
        return _runner.load();
    }
}
