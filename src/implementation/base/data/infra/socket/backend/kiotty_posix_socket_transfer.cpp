#if !defined(_WIN32)

#include "base/data/infra/socket/kiotty_socket_transfer.h"

#include "base/data/infra/error/kiotty_socket_error.h"
#include "base/data/infra/socket/backend/kiotty_posix_address.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>

#include <cerrno>

namespace kiotty::net::implementation
{
    namespace
    {
        TransferResult receiveWith(const ActiveSocket& socket, Span<char> buffer, int flags);
    }

    TransferResult send(const ActiveSocket& socket, Span<const char> data)
    {
        if (!socket.valid()) { return TransferResult::failure(SocketCode::INVALID_ARGUMENT); }
        const int descriptor = descriptorOf(socket.handle());
        const int flags = sendFlags();
        for (;;)
        {
            const ssize_t sent = ::send(descriptor, data.data, data.length, flags);
            if (sent >= 0) { return TransferResult::success(static_cast<size_t>(sent)); }
            if (errno != EINTR) { return TransferResult::failure(lastSocketCode()); }
        }
    }

    TransferResult receive(const ActiveSocket& socket, Span<char> buffer)
    {
        return receiveWith(socket, buffer, 0);
    }

    TransferResult peek(const ActiveSocket& socket, Span<char> buffer)
    {
        return receiveWith(socket, buffer, MSG_PEEK);
    }

    SocketCode waitReadable(const ActiveSocket& socket, uint32_t timeout_milliseconds)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        const int descriptor = descriptorOf(socket.handle());
        return waitFor(descriptor, POLLIN, timeout_milliseconds, socket.traits().stop);
    }

    SocketCode waitWritable(const ActiveSocket& socket, uint32_t timeout_milliseconds)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        const int descriptor = descriptorOf(socket.handle());
        return waitFor(descriptor, POLLOUT, timeout_milliseconds, socket.traits().stop);
    }

    SocketCode shutdownWrite(const ActiveSocket& socket)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        const int done = ::shutdown(descriptorOf(socket.handle()), SHUT_WR);
        if (done < 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
    }

    SocketCode configureNoDelay(const ActiveSocket& socket, NoDelay no_delay)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        const bool is_pipe = socket.traits().transport == KIOTTY_NET_TRANSPORT_PIPE;
        if (is_pipe) { return SocketCode::SUCCESS; }
        const int value = no_delay == NoDelay::ENABLED ? 1 : 0;
        const int descriptor = descriptorOf(socket.handle());
        const int set =
            ::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
        if (set < 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
    }

    namespace
    {
        TransferResult receiveWith(const ActiveSocket& socket, Span<char> buffer, int flags)
        {
            if (!socket.valid()) { return TransferResult::failure(SocketCode::INVALID_ARGUMENT); }
            const int descriptor = descriptorOf(socket.handle());
            for (;;)
            {
                const ssize_t received = ::recv(descriptor, buffer.data, buffer.length, flags);
                if (received > 0) { return TransferResult::success(static_cast<size_t>(received)); }
                const bool is_closed = received == 0 && buffer.length > 0;
                if (is_closed) { return TransferResult::failure(SocketCode::CLOSED); }
                if (received == 0) { return TransferResult::success(static_cast<size_t>(0)); }
                if (errno != EINTR) { return TransferResult::failure(lastSocketCode()); }
            }
        }
    }
}

#endif
