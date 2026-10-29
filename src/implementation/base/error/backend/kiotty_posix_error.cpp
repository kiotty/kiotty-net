#include "base/error/kiotty_socket_error.h"

#include <cerrno>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            const SocketErrorEntry TABLE[] =
            {
                { EAGAIN, SocketCode::WOULD_BLOCK },
                { EWOULDBLOCK, SocketCode::WOULD_BLOCK },
                { EINPROGRESS, SocketCode::PENDING },
                { EALREADY, SocketCode::PENDING },
                { EPIPE, SocketCode::CLOSED },
                { ENOTCONN, SocketCode::CLOSED },
                { ECANCELED, SocketCode::CANCELLED },
                { ETIMEDOUT, SocketCode::TIMEOUT },
                { ECONNREFUSED, SocketCode::REFUSED },
                { ENOENT, SocketCode::REFUSED },
                { ECONNRESET, SocketCode::RESET },
                { ECONNABORTED, SocketCode::RESET },
                { ENETRESET, SocketCode::RESET },
                { EHOSTUNREACH, SocketCode::UNREACHABLE },
                { ENETUNREACH, SocketCode::UNREACHABLE },
                { ENETDOWN, SocketCode::UNREACHABLE },
                { EHOSTDOWN, SocketCode::UNREACHABLE },
                { EADDRINUSE, SocketCode::ADDRESS_IN_USE },
                { EADDRNOTAVAIL, SocketCode::INVALID_ARGUMENT },
                { EINVAL, SocketCode::INVALID_ARGUMENT },
                { EFAULT, SocketCode::INVALID_ARGUMENT },
                { EBADF, SocketCode::INVALID_ARGUMENT },
                { ENOTSOCK, SocketCode::INVALID_ARGUMENT },
                { ENAMETOOLONG, SocketCode::INVALID_ARGUMENT },
                { EISCONN, SocketCode::INVALID_ARGUMENT },
                { EMFILE, SocketCode::OUT_OF_RESOURCE },
                { ENFILE, SocketCode::OUT_OF_RESOURCE },
                { ENOBUFS, SocketCode::OUT_OF_RESOURCE },
                { ENOMEM, SocketCode::OUT_OF_RESOURCE },
                { EAFNOSUPPORT, SocketCode::NOT_SUPPORTED },
                { EPROTONOSUPPORT, SocketCode::NOT_SUPPORTED },
                { EOPNOTSUPP, SocketCode::NOT_SUPPORTED }
            };
        }

        Span<const SocketErrorEntry> socketErrorTable()
        {
            Span<const SocketErrorEntry> table = { TABLE, sizeof(TABLE) / sizeof(TABLE[0]) };
            return table;
        }

        SocketCode lastSocketCode()
        {
            return toSocketCode(errno);
        }
    }
}
