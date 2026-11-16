#include "http/data/repository/client/kiotty_http_client_link.h"

namespace kiotty::net::implementation
{
    HttpClientLink::HttpClientLink(StreamClient& stream, StopSignal& stop, BusyFlag& busy)
        : _stream(stream)
        , _stop(stop)
        , _busy(busy)
    {
    }

    void HttpClientLink::disconnect()
    {
        const BusyGuard guard(_busy);
        if (!guard.isHeld()) { return; }
        static_cast<void>(_stop.accept());
        _stream.disconnect();
    }

    void HttpClientLink::close()
    {
        if (!_busy.enterClose())
        {
            _stop.raise();
            return;
        }
        _stream.disconnect();
        _busy.leave();
    }
}
