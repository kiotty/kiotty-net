#include "data/repository/connection/kiotty_connection_host.h"

#include <cstddef>
#include <utility>

namespace kiotty::net::implementation
{
    ConnectionHost::ConnectionHost(ReusablePoolBase<Connection>& connections)
        : _connections(connections)
        , _limits()
        , _max_connections(0)
        , _active()
        , _notification()
    {
    }

    void ConnectionHost::initialize(ConnectionLimits limits,
                                    size_t max_connections,
                                    TlsCapacity cipher)
    {
        _limits = limits;
        _max_connections = max_connections;
        _active.clear();
        _active.reserve(max_connections);
        for (size_t i = 0; i < _connections.capacity(); ++i)
        {
            _connections.at(i).reserve(limits, cipher);
        }
    }

    ConnectionResult ConnectionHost::acquire(ConnectionConfiguration&& configuration)
    {
        configuration.limits = _limits;
        const std::lock_guard<std::mutex> lock(_notification);
        if (full()) { return ConnectionResult::failure(KIOTTY_NET_CODE_TABLE_FULL); }
        ConnectionResult acquired = _connections.acquire(std::move(configuration));
        if (acquired) { _active.push_back(acquired.value().slot()); }
        return acquired;
    }

    void ConnectionHost::release(Connection& connection)
    {
        const std::lock_guard<std::mutex> lock(_notification);
        const int32_t slot = connection.slot();
        for (size_t i = 0; i < _active.size(); ++i)
        {
            if (_active[i] != slot) { continue; }
            _active[i] = _active.back();
            _active.pop_back();
            break;
        }
        _connections.release(connection);
    }

    Connection& ConnectionHost::at(int32_t slot)
    {
        return _connections.at(static_cast<size_t>(slot));
    }

    Span<const int32_t> ConnectionHost::active() const
    {
        return Span<const int32_t>{ _active.data(), _active.size() };
    }

    bool ConnectionHost::full() const
    {
        return _connections.count() >= _max_connections;
    }

    bool ConnectionHost::empty() const
    {
        return _connections.count() == 0;
    }

    std::mutex& ConnectionHost::notificationMutex()
    {
        return _notification;
    }
}
