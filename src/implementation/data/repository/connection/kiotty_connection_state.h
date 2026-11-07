#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "domain/entity/kiotty_phase.h"
#include "kiotty_net_base.h"

#include <cstddef>
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

    static constexpr size_t CONNECTION_STATE_TAIL_BYTES = 4;
    static constexpr size_t CONNECTION_STATE_BYTES = sizeof(uint64_t)
                                                   + sizeof(KiottyNetConnectionContext)
                                                   + sizeof(PoolHandle)
                                                   + sizeof(KiottyNetConnectionInfo)
                                                   + CONNECTION_STATE_TAIL_BYTES;
    static_assert(sizeof(ConnectionState) == CONNECTION_STATE_BYTES,
                  "ConnectionState: unexpected padding");

    inline bool ConnectionState::isExpired(uint64_t now) const
    {
        return deadline != 0 && now >= deadline;
    }
}
