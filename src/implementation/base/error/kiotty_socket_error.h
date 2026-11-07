#if !defined(KIOTTY_SOCKET_ERROR_H)
#define KIOTTY_SOCKET_ERROR_H

#include "core/kiotty_span.h"
#include "domain/entity/kiotty_socket_code.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct SocketErrorEntry
    {
        int        os_error;
        SocketCode code;
    };

    Span<const SocketErrorEntry> socketErrorTable();
    SocketCode                   toSocketCode(int os_error);
    SocketCode                   lastSocketCode();
}

#endif
