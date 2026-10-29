#include "base/error/kiotty_socket_error.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <winsock2.h>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            const SocketErrorEntry TABLE[] =
            {
                { WSAEWOULDBLOCK, SocketCode::WOULD_BLOCK },
                { WSA_IO_PENDING, SocketCode::PENDING },
                { WSAEINPROGRESS, SocketCode::PENDING },
                { WSAEALREADY, SocketCode::PENDING },
                { WSAESHUTDOWN, SocketCode::CLOSED },
                { WSAENOTCONN, SocketCode::CLOSED },
                { WSAEDISCON, SocketCode::CLOSED },
                { WSA_OPERATION_ABORTED, SocketCode::CANCELLED },
                { WSAETIMEDOUT, SocketCode::TIMEOUT },
                { WSAECONNREFUSED, SocketCode::REFUSED },
                { ERROR_CONNECTION_REFUSED, SocketCode::REFUSED },
                { WSAECONNRESET, SocketCode::RESET },
                { WSAECONNABORTED, SocketCode::RESET },
                { WSAENETRESET, SocketCode::RESET },
                { ERROR_NETNAME_DELETED, SocketCode::RESET },
                { ERROR_CONNECTION_ABORTED, SocketCode::RESET },
                { WSAEHOSTUNREACH, SocketCode::UNREACHABLE },
                { WSAENETUNREACH, SocketCode::UNREACHABLE },
                { WSAENETDOWN, SocketCode::UNREACHABLE },
                { WSAEHOSTDOWN, SocketCode::UNREACHABLE },
                { WSAEADDRINUSE, SocketCode::ADDRESS_IN_USE },
                { WSAEADDRNOTAVAIL, SocketCode::INVALID_ARGUMENT },
                { WSAEINVAL, SocketCode::INVALID_ARGUMENT },
                { WSAEFAULT, SocketCode::INVALID_ARGUMENT },
                { WSAEBADF, SocketCode::INVALID_ARGUMENT },
                { WSA_INVALID_HANDLE, SocketCode::INVALID_ARGUMENT },
                { WSAENOTSOCK, SocketCode::INVALID_ARGUMENT },
                { WSAENAMETOOLONG, SocketCode::INVALID_ARGUMENT },
                { WSAEISCONN, SocketCode::INVALID_ARGUMENT },
                { WSANOTINITIALISED, SocketCode::INVALID_ARGUMENT },
                { WSAEMFILE, SocketCode::OUT_OF_RESOURCE },
                { WSAENOBUFS, SocketCode::OUT_OF_RESOURCE },
                { WSA_NOT_ENOUGH_MEMORY, SocketCode::OUT_OF_RESOURCE },
                { WSAEAFNOSUPPORT, SocketCode::NOT_SUPPORTED },
                { WSAEPROTONOSUPPORT, SocketCode::NOT_SUPPORTED },
                { WSAEOPNOTSUPP, SocketCode::NOT_SUPPORTED }
            };
        }

        Span<const SocketErrorEntry> socketErrorTable()
        {
            Span<const SocketErrorEntry> table = { TABLE, sizeof(TABLE) / sizeof(TABLE[0]) };
            return table;
        }

        SocketCode lastSocketCode()
        {
            return toSocketCode(::WSAGetLastError());
        }
    }
}
