#pragma once

#include "core/kiotty_span.h"
#include "domain/entity/kiotty_socket_code.h"

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
