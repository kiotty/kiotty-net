#include "data/repository/connection/kiotty_work_flow.h"

#include "data/repository/connection/kiotty_adopt_action.h"

namespace kiotty::net::implementation
{
    namespace
    {
        bool isReadyToWork(WorkContext::State state, const IoEvent& read, const IoEvent& write);
    }

    WorkFlow::WorkFlow(const ConnectionParts& parts)
        : _receive(parts.receive)
        , _send(parts.send)
        , _tls(parts.tls)
        , _socket(parts.socket)
        , _work(parts.work)
        , _reporter(parts.work)
        , _read_event(parts.read_event)
        , _write_event(parts.write_event)
        , _phase(parts.phase)
    {
    }

    KiottyNetCode WorkFlow::verify(int32_t generation) const
    {
        return _reporter.verify(generation);
    }

    ConnectionStream WorkFlow::stream()
    {
        return ConnectionStream(_socket, _tls, _receive, _send);
    }

    KiottyNetCode WorkFlow::closeStream(int32_t generation, const KiottyNetConnectionAction& then)
    {
        const KiottyNetCode verified = _reporter.verify(generation);
        if (verified != KIOTTY_NET_CODE_SUCCESS) { return verified; }
        const bool starts_work = then.type == KIOTTY_NET_ACTION_TYPE_START_WORK;
        if (starts_work) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const AdoptResult outcome = adoptAction(then, _send);
        if (!outcome) { return outcome.code; }
        _reporter.finish(outcome.value);
        const Phase phase = _phase.load();
        if (phase == Phase::CLOSING)
        {
            return KIOTTY_NET_CODE_CLOSED;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    IoAction WorkFlow::takeOutcome()
    {
        return _work.takeOutcome();
    }

    bool WorkFlow::isReady() const
    {
        return isReadyToWork(_work.state(), _read_event, _write_event);
    }

    bool WorkFlow::isFinished() const
    {
        return _work.state() == WorkContext::State::DONE;
    }

    bool WorkFlow::isWorking() const
    {
        return _work.state() != WorkContext::State::IDLE;
    }

    namespace
    {
        bool isReadyToWork(WorkContext::State state, const IoEvent& read, const IoEvent& write)
        {
            const bool requested = state == WorkContext::State::REQUESTED;
            const bool events_idle = !read.pending && !write.pending;
            return requested && events_idle;
        }
    }
}
