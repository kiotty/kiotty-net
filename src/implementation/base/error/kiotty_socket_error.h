#if !defined(KIOTTY_SOCKET_ERROR_H)
#define KIOTTY_SOCKET_ERROR_H

#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty
{
    namespace net
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

        struct SocketErrorEntry
        {
            int        os_error;
            SocketCode code;
        };

        Span<const SocketErrorEntry> socketErrorTable();
        SocketCode                   toSocketCode(int os_error);
        SocketCode                   lastSocketCode();
        KiottyNetCode                toNetCode(SocketCode code);
    }
}

#endif
