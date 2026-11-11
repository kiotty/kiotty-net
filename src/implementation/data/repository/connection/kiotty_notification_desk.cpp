#include "data/repository/connection/kiotty_notification_desk.h"

#include "domain/entity/kiotty_phase.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    NotificationDesk::NotificationDesk(ReusablePoolBase<Connection>& connections,
                                       std::mutex& notification)
        : _connections(connections)
        , _notification(notification)
    {
    }

    KiottyNetCode NotificationDesk::notify(PoolHandle id, KiottyNetSpan data)
    {
        return notify(id, Span<const KiottyNetSpan>{ &data, 1 });
    }

    KiottyNetCode NotificationDesk::notify(PoolHandle id, Span<const KiottyNetSpan> pieces)
    {
        const std::lock_guard<std::mutex> lock(_notification);
        const ConnectionResult found = _connections.get(id);
        if (!found) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        const ConnectionState state = found.value().state();
        if (!state.is_alive) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        return found.value().flows().send.stageNotification(pieces);
    }

    KiottyNetCode NotificationDesk::broadcast(KiottyNetSpan data)
    {
        return broadcast(Span<const KiottyNetSpan>{ &data, 1 });
    }

    KiottyNetCode NotificationDesk::broadcast(Span<const KiottyNetSpan> pieces)
    {
        size_t total = 0;
        for (const KiottyNetSpan& piece : pieces)
        {
            if (piece.data == nullptr && piece.length > 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            total += piece.length;
        }
        const std::lock_guard<std::mutex> lock(_notification);
        const bool fitting = canTakeAll(total);
        if (!fitting) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        for (size_t i = 0; i < _connections.capacity(); ++i)
        {
            const bool target = isTarget(i);
            if (target)
            {
                (void)_connections.at(i).flows().send.stageNotification(pieces);
            }
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetConnectionInfo NotificationDesk::info(PoolHandle id) const
    {
        const std::lock_guard<std::mutex> lock(_notification);
        const ConnectionResult found = _connections.get(id);
        if (!found) { return KiottyNetConnectionInfo{}; }
        return found.value().state().info;
    }

    ConnectionResult NotificationDesk::working(PoolHandle id)
    {
        const ConnectionResult found = find(id);
        if (!found) { return found; }
        const KiottyNetCode verified = found.value().flows().work.verify(id.generation);
        if (verified != KIOTTY_NET_CODE_SUCCESS) { return ConnectionResult::failure(verified); }
        return found;
    }

    KiottyNetCode NotificationDesk::closeStream(PoolHandle id,
                                                const KiottyNetConnectionAction& then)
    {
        const ConnectionResult found = find(id);
        if (!found) { return found.code; }
        return found.value().flows().work.closeStream(id.generation, then);
    }

    PoolHandle NotificationDesk::findReady() const
    {
        const std::lock_guard<std::mutex> lock(_notification);
        for (size_t i = 0; i < _connections.capacity(); ++i)
        {
            const bool is_ready = _connections.active(i)
                                  && _connections.at(i).status().phase == Phase::READY;
            if (is_ready) { return _connections.at(i).state().id; }
        }
        return PoolHandle{ 0, 0 };
    }

    ConnectionResult NotificationDesk::find(PoolHandle id)
    {
        const std::lock_guard<std::mutex> lock(_notification);
        return _connections.get(id);
    }

    bool NotificationDesk::isTarget(size_t index)
    {
        const bool active = _connections.active(index);
        if (!active)
        {
            return false;
        }
        return _connections.at(index).state().is_alive;
    }

    bool NotificationDesk::canTakeAll(size_t length)
    {
        for (size_t i = 0; i < _connections.capacity(); ++i)
        {
            const bool target = isTarget(i);
            if (!target)
            {
                continue;
            }
            const size_t writable = _connections.at(i).flows().send.notificationWritable();
            if (writable < length) { return false; }
        }
        return true;
    }
}
