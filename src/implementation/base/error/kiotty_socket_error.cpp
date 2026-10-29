#include "base/error/kiotty_socket_error.h"

namespace kiotty
{
    namespace net
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

        KiottyNetCode toNetCode(SocketCode code)
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
            default:
                return KIOTTY_SOCKET_ERROR;
            }
        }
    }
}
