#include "data/repository/connection/kiotty_notification_desk.h"

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
        const std::lock_guard<std::mutex> lock(_notification);
        const ConnectionResult found = _connections.get(id);
        if (!found) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        const ConnectionState state = found.value().state();
        if (!state.is_alive) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        return found.value().flows().send.stageNotification(data);
    }

    KiottyNetCode NotificationDesk::broadcast(KiottyNetSpan data)
    {
        if (data.data == nullptr && data.length > 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const std::lock_guard<std::mutex> lock(_notification);
        const bool fitting = canTakeAll(data.length);
        if (!fitting) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        for (size_t i = 0; i < _connections.capacity(); ++i)
        {
            const bool target = isTarget(i);
            if (target)
            {
                (void)_connections.at(i).flows().send.stageNotification(data);
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
