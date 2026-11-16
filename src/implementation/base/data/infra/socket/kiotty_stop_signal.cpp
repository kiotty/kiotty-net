#include "base/data/infra/socket/kiotty_stop_signal.h"

namespace kiotty::net::implementation
{
    StopSignal::StopSignal()
        : _raised(0)
        , _accepted(0)
        , _reader(INVALID_SOCKET_HANDLE)
        , _writer(INVALID_SOCKET_HANDLE)
    {
    }

    StopSignal::~StopSignal()
    {
        close();
    }

    void StopSignal::raise()
    {
        _raised.fetch_add(1);
        notify();
    }

    bool StopSignal::accept()
    {
        drain();
        const uint32_t raised = _raised.load();
        const bool is_raised = raised != _accepted;
        _accepted = raised;
        return is_raised;
    }

    bool StopSignal::isRaised() const
    {
        return _raised.load() != _accepted;
    }

    SocketHandle StopSignal::handle() const
    {
        return _reader;
    }
}
