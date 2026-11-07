#pragma once

#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class SocketCode : uint8_t
    {
        SUCCESS,
        WOULD_BLOCK,
        PENDING,
        CLOSED,
        CANCELLED,
        TIMEOUT,
        REFUSED,
        RESET,
        UNREACHABLE,
        ADDRESS_IN_USE,
        INVALID_ARGUMENT,
        OUT_OF_RESOURCE,
        NOT_SUPPORTED,
        BUSY,
        UNKNOWN
    };

    inline KiottyNetCode toNetCode(SocketCode code)
    {
        switch (code)
        {
        case SocketCode::SUCCESS:
            return KIOTTY_SUCCESS;
        case SocketCode::CLOSED:
            return KIOTTY_PEER_CLOSED;
        case SocketCode::CANCELLED:
            return KIOTTY_CLOSED;
        case SocketCode::TIMEOUT:
            return KIOTTY_TIMEOUT;
        case SocketCode::BUSY:
            return KIOTTY_BUSY;
        case SocketCode::WOULD_BLOCK:
        case SocketCode::PENDING:
        case SocketCode::REFUSED:
        case SocketCode::RESET:
        case SocketCode::UNREACHABLE:
        case SocketCode::ADDRESS_IN_USE:
        case SocketCode::INVALID_ARGUMENT:
        case SocketCode::OUT_OF_RESOURCE:
        case SocketCode::NOT_SUPPORTED:
        case SocketCode::UNKNOWN:
            return KIOTTY_SOCKET_ERROR;
        }
        return KIOTTY_SOCKET_ERROR;
    }
}
