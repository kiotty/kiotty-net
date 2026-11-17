#include "http/data/repository/client/kiotty_http_client_link.h"

#include "core/kiotty_busy_guard.h"

namespace kiotty::net::implementation
{
    HttpClientLink::HttpClientLink(HttpClientState& state, StreamClient& line)
        : _state(state)
        , _line(line)
    {
    }

    void HttpClientLink::disconnect()
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return; }
        static_cast<void>(_line.stop().accept());
        _line.disconnect();
    }

    void HttpClientLink::close()
    {
        if (!_state.busy.enterClose())
        {
            _line.stop().raise();
            return;
        }
        _line.disconnect();
        _state.busy.leave();
    }
}
