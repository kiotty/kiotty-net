#pragma once

#include "core/pool/kiotty_reusable_pool.h"
#include "data/repository/connection/kiotty_connection.h"
#include "data/repository/connection/kiotty_connection_configuration.h"
#include "domain/entity/kiotty_connection_limits.h"

#include <cstdint>
#include <mutex>

namespace kiotty::net::implementation
{
    class ConnectionHost
    {
    public:
        explicit ConnectionHost(ReusablePoolBase<Connection>& connections);
        ConnectionHost(const ConnectionHost&) = delete;
        ConnectionHost& operator=(const ConnectionHost&) = delete;

        void             initialize(ConnectionLimits limits);
        ConnectionResult acquire(ConnectionConfiguration&& configuration);
        void             release(Connection& connection);
        Connection&      at(int32_t slot);
        int32_t          capacity() const;
        bool             full() const;
        bool             empty() const;
        std::mutex&      notificationMutex();

    private:
        ReusablePoolBase<Connection>& _connections;
        ConnectionLimits              _limits;
        std::mutex                    _notification;
    };
}
