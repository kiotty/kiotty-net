#include "base/error/kiotty_socket_error.h"

namespace kiotty::net::implementation
{
    SocketCode toSocketCode(int os_error)
    {
        if (os_error == 0)
        {
            return SocketCode::SUCCESS;
        }
        Span<const SocketErrorEntry> table = socketErrorTable();
        for (const SocketErrorEntry& entry : table)
        {
            if (entry.os_error == os_error)
            {
                return entry.code;
            }
        }
        return SocketCode::UNKNOWN;
    }
}
