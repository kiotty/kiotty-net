#include "data/repository/connection/kiotty_work_flow.h"

#include "data/repository/connection/kiotty_adopt_action.h"

namespace kiotty::net::implementation
{
    WorkFlow::WorkFlow(const ConnectionParts& parts)
        : _receive(parts.receive)
        , _send(parts.send)
        , _tls(parts.tls)
        , _socket(parts.socket)
        , _work(parts.work)
        , _read_event(parts.read_event)
        , _write_event(parts.write_event)
        , _phase(parts.phase)
    {
    }

    KiottyNetCode WorkFlow::verify(int32_t generation) const
    {
        return _work.verify(generation);
    }

    ConnectionStream WorkFlow::stream()
    {
        return ConnectionStream(_socket, _tls, _receive, _send);
    }

    KiottyNetCode WorkFlow::closeStream(int32_t generation, const KiottyNetConnectionAction& then)
    {
        const KiottyNetCode verified = _work.verify(generation);
        if (verified != KIOTTY_NET_CODE_SUCCESS) { return verified; }
        if (then.type == KIOTTY_NET_ACTION_TYPE_START_WORK) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const AdoptResult outcome = adoptAction(then, _send);
        if (!outcome) { return outcome.code; }
        _work.finish(outcome.value);
        const Phase phase = _phase.load();
        if (phase == Phase::CLOSING)
        {
            return KIOTTY_NET_CODE_CLOSED;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    IoAction WorkFlow::takeOutcome()
    {
        return _work.onFinished();
    }

    bool WorkFlow::isReady() const
    {
        const bool requested = _work.requested();
        return requested && !_read_event.pending && !_write_event.pending;
    }

    bool WorkFlow::isFinished() const
    {
        return _work.finished();
    }

    bool WorkFlow::isWorking() const
    {
        return _work.working();
    }
}
