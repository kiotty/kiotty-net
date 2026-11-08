#include "data/repository/connection/kiotty_connection_host.h"

#include <cstddef>
#include <utility>

namespace kiotty::net::implementation
{
    ConnectionHost::ConnectionHost(ReusablePoolBase<Connection>& connections)
        : _connections(connections)
        , _limits()
        , _max_connections(0)
        , _notification()
    {
    }

    void ConnectionHost::initialize(ConnectionLimits limits, size_t max_connections)
    {
        _limits = limits;
        _max_connections = max_connections;
    }

    ConnectionResult ConnectionHost::acquire(ConnectionConfiguration&& configuration)
    {
        configuration.limits = _limits;
        const std::lock_guard<std::mutex> lock(_notification);
        if (full()) { return ConnectionResult::failure(KIOTTY_NET_CODE_TABLE_FULL); }
        return _connections.acquire(std::move(configuration));
    }

    void ConnectionHost::release(Connection& connection)
    {
        const std::lock_guard<std::mutex> lock(_notification);
        _connections.release(connection);
    }

    Connection& ConnectionHost::at(int32_t slot)
    {
        return _connections.at(static_cast<size_t>(slot));
    }

    int32_t ConnectionHost::capacity() const
    {
        return static_cast<int32_t>(_connections.capacity());
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
