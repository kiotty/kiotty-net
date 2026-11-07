#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "domain/entity/kiotty_phase.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct ConnectionState
    {
        uint64_t                   deadline;
        KiottyNetConnectionContext context;
        PoolHandle                 id;
        KiottyNetConnectionInfo    info;
        Phase                      phase;
        bool                       is_alive;
        uint8_t                    _padding[2];

        bool isExpired(uint64_t now) const;
    };

    static_assert(sizeof(ConnectionState) == sizeof(uint64_t) + sizeof(KiottyNetConnectionContext) + sizeof(PoolHandle) + sizeof(KiottyNetConnectionInfo) + 4, "ConnectionState: unexpected padding");

    inline bool ConnectionState::isExpired(uint64_t now) const
    {
        return deadline != 0 && now >= deadline;
    }
}
