#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "data/repository/connection/kiotty_connection.h"
#include "kiotty_net_base.h"

#include <mutex>

namespace kiotty::net::implementation
{
    class NotificationDesk
    {
    public:
        NotificationDesk(ReusablePoolBase<Connection>& connections, std::mutex& notification);
        NotificationDesk(const NotificationDesk&) = delete;
        NotificationDesk& operator=(const NotificationDesk&) = delete;

        KiottyNetCode           notify(PoolHandle id, KiottyNetSpan data);
        KiottyNetCode           broadcast(KiottyNetSpan data);
        KiottyNetConnectionInfo info(PoolHandle id) const;
        ConnectionResult        working(PoolHandle id);
        KiottyNetCode           closeStream(PoolHandle id, const KiottyNetConnectionAction& then);
        PoolHandle              findReady() const;

    private:
        ConnectionResult find(PoolHandle id);
        bool             isTarget(size_t index);
        bool             canTakeAll(size_t length);

        ReusablePoolBase<Connection>& _connections;
        std::mutex&                   _notification;
    };
}
