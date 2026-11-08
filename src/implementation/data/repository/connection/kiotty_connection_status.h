#pragma once

#include "data/repository/connection/kiotty_connection_state.h"
#include "domain/entity/kiotty_phase.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct ConnectionStatus
    {
        uint64_t deadline;
        Phase    phase;
        uint8_t  pending;

        bool isExpired(uint64_t now) const;
        bool isSettled() const;
    };

    static_assert(sizeof(ConnectionStatus) == 16, "ConnectionStatus: unexpected padding");

    inline bool ConnectionStatus::isExpired(uint64_t now) const
    {
        return deadline != 0 && now >= deadline;
    }

    inline bool ConnectionStatus::isSettled() const
    {
        return pending == PENDING_NONE;
    }
}
