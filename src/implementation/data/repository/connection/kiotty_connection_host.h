#pragma once

#include "core/kiotty_span.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "data/repository/connection/kiotty_connection.h"
#include "data/repository/connection/kiotty_connection_configuration.h"
#include "domain/entity/kiotty_connection_limits.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kiotty::net::implementation
{
    class ConnectionHost
    {
    public:
        explicit ConnectionHost(ReusablePoolBase<Connection>& connections);
        ConnectionHost(const ConnectionHost&) = delete;
        ConnectionHost& operator=(const ConnectionHost&) = delete;

        void             initialize(ConnectionLimits limits, size_t max_connections);
        ConnectionResult acquire(ConnectionConfiguration&& configuration);
        void             release(Connection& connection);
        Connection&      at(int32_t slot);
        Span<const int32_t> active() const;
        bool             full() const;
        bool             empty() const;
        std::mutex&      notificationMutex();

    private:
        ReusablePoolBase<Connection>& _connections;
        ConnectionLimits              _limits;
        size_t                        _max_connections;
        std::vector<int32_t>          _active;
        std::mutex                    _notification;
    };
}
