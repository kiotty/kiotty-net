#if !defined(KIOTTY_CONNECTION_HOST_H)
#define KIOTTY_CONNECTION_HOST_H

#include "base/connection/kiotty_connection.h"
#include "core/kiotty_result.h"
#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

namespace kiotty::net::implementation
{
    typedef Result<Connection&, KiottyNetCode> ConnectionResult;

    template <size_t N>
    class ConnectionHost
    {
    public:
        ConnectionHost();
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

        KiottyNetCode           notify(PoolHandle id, KiottyNetSpan data);
        KiottyNetCode           broadcast(KiottyNetSpan data);
        KiottyNetConnectionInfo info(PoolHandle id) const;

        ConnectionResult working(PoolHandle id);
        KiottyNetCode    closeStream(PoolHandle id, const KiottyNetConnectionAction& then);

    private:
        ConnectionResult find(PoolHandle id);

        mutable std::mutex          _notification;
        ConnectionLimits            _limits;
        ReusablePool<Connection, N> _connections;
    };

    template <size_t N>
    ConnectionHost<N>::ConnectionHost()
        : _notification()
        , _limits()
        , _connections()
    {
    }

    template <size_t N>
    void ConnectionHost<N>::initialize(ConnectionLimits limits)
    {
        _limits = limits;
    }

    template <size_t N>
    ConnectionResult ConnectionHost<N>::acquire(ConnectionConfiguration&& configuration)
    {
        configuration.limits = _limits;
        std::lock_guard<std::mutex> lock(_notification);
        typename ReusablePool<Connection, N>::AcquireResult acquired = _connections.acquire(std::move(configuration));
        if (!acquired)
        {
            return ConnectionResult::failure(acquired.code);
        }
        return ConnectionResult::success(acquired.value());
    }

    template <size_t N>
    void ConnectionHost<N>::release(Connection& connection)
    {
        std::lock_guard<std::mutex> lock(_notification);
        _connections.release(connection);
    }

    template <size_t N>
    Connection& ConnectionHost<N>::at(int32_t slot)
    {
        return _connections.at(static_cast<size_t>(slot));
    }

    template <size_t N>
    int32_t ConnectionHost<N>::capacity() const
    {
        return static_cast<int32_t>(N);
    }

    template <size_t N>
    bool ConnectionHost<N>::full() const
    {
        std::lock_guard<std::mutex> lock(_notification);
        return _connections.count() == _connections.capacity();
    }

    template <size_t N>
    bool ConnectionHost<N>::empty() const
    {
        std::lock_guard<std::mutex> lock(_notification);
        return _connections.count() == 0;
    }

    template <size_t N>
    std::mutex& ConnectionHost<N>::notificationMutex()
    {
        return _notification;
    }

    template <size_t N>
    KiottyNetCode ConnectionHost<N>::notify(PoolHandle id, KiottyNetSpan data)
    {
        std::lock_guard<std::mutex> lock(_notification);
        const ConnectionResult found = _connections.get(id);
        if (!found || !found.value().alive()) { return KIOTTY_PEER_CLOSED; }
        return found.value().stageNotification(data);
    }

    template <size_t N>
    KiottyNetCode ConnectionHost<N>::broadcast(KiottyNetSpan data)
    {
        if (data.data == nullptr && data.length > 0)
        {
            return KIOTTY_INVALID_ARGUMENT;
        }
        std::lock_guard<std::mutex> lock(_notification);
        for (size_t i = 0; i < N; ++i)
        {
            Connection& connection = _connections.at(i);
            bool target = _connections.active(i) && connection.alive();
            if (target && connection.notificationWritable() < data.length)
            {
                return KIOTTY_BUFFER_FULL;
            }
        }
        for (size_t i = 0; i < N; ++i)
        {
            Connection& connection = _connections.at(i);
            if (_connections.active(i) && connection.alive())
            {
                (void)connection.stageNotification(data);
            }
        }
        return KIOTTY_SUCCESS;
    }

    template <size_t N>
    KiottyNetConnectionInfo ConnectionHost<N>::info(PoolHandle id) const
    {
        std::lock_guard<std::mutex> lock(_notification);
        const auto found = _connections.get(id);
        if (!found) { return KiottyNetConnectionInfo{}; }
        return found.value().info();
    }

    template <size_t N>
    ConnectionResult ConnectionHost<N>::working(PoolHandle id)
    {
        const ConnectionResult found = find(id);
        if (!found) { return found; }
        const KiottyNetCode verified = found.value().verifyWork(id.generation);
        if (verified != KIOTTY_SUCCESS) { return ConnectionResult::failure(verified); }
        return found;
    }

    template <size_t N>
    KiottyNetCode ConnectionHost<N>::closeStream(PoolHandle id, const KiottyNetConnectionAction& then)
    {
        const ConnectionResult found = find(id);
        if (!found) { return found.code; }
        return found.value().closeStream(id.generation, then);
    }

    template <size_t N>
    ConnectionResult ConnectionHost<N>::find(PoolHandle id)
    {
        std::lock_guard<std::mutex> lock(_notification);
        return _connections.get(id);
    }
}

#endif
