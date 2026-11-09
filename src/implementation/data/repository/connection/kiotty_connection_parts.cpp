#include "data/repository/connection/kiotty_connection.h"

namespace kiotty::net::implementation
{
    void Connection::reserve(ConnectionLimits limits, TlsCapacity cipher)
    {
        _plain.reserve(limits.receive, limits.send, limits.notify);
        reserveTlsStages(_tls.stages(), cipher);
    }

    Caller Connection::callerFor(const Handler& handler)
    {
        return Caller{ handler, _info, _plain.send, _context, _id };
    }

    ConnectionParts Connection::parts()
    {
        return ConnectionParts{ _phase, _work, _read_event, _write_event,
                                _plain.receive, _plain.send, _plain.notify, _tls, _socket };
    }
}
