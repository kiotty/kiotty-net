#if defined(_WIN32)

#include "data/infra/socket/kiotty_socket_transfer.h"

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/socket/backend/kiotty_windows_address.h"

#include <windows.h>

namespace kiotty::net::implementation
{
    namespace
    {
        TransferResult receiveWith(const ActiveSocket& socket, Span<char> buffer, int flags);
    }

    TransferResult send(const ActiveSocket& socket, Span<const char> data)
    {
        if (!socket.valid()) { return TransferResult::failure(SocketCode::INVALID_ARGUMENT); }
        const int sent = ::send(socketOf(socket.handle()), data.data, clampLength(data.length), 0);
        if (sent == SOCKET_ERROR) { return TransferResult::failure(lastSocketCode()); }
        return TransferResult::success(static_cast<size_t>(sent));
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
        return waitReady(socketOf(socket.handle()), WaitDirection::READ, timeout_milliseconds);
    }

    SocketCode waitWritable(const ActiveSocket& socket, uint32_t timeout_milliseconds)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        return waitReady(socketOf(socket.handle()), WaitDirection::WRITE, timeout_milliseconds);
    }

    SocketCode shutdownWrite(const ActiveSocket& socket)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        const int done = ::shutdown(socketOf(socket.handle()), SD_SEND);
        if (done != 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
    }

    SocketCode configureNoDelay(const ActiveSocket& socket, NoDelay no_delay)
    {
        if (!socket.valid()) { return SocketCode::INVALID_ARGUMENT; }
        if (socket.transport() == KIOTTY_NET_TRANSPORT_PIPE) { return SocketCode::SUCCESS; }
        const BOOL value = no_delay == NoDelay::ENABLED ? TRUE : FALSE;
        const char* option = reinterpret_cast<const char*>(&value);
        const SOCKET native = socketOf(socket.handle());
        const int set = ::setsockopt(native, IPPROTO_TCP, TCP_NODELAY, option, sizeof(value));
        if (set != 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
    }

    namespace
    {
        TransferResult receiveWith(const ActiveSocket& socket, Span<char> buffer, int flags)
        {
            if (!socket.valid()) { return TransferResult::failure(SocketCode::INVALID_ARGUMENT); }
            const SOCKET native = socketOf(socket.handle());
            const int length = clampLength(buffer.length);
            const int received = ::recv(native, buffer.data, length, flags);
            if (received == SOCKET_ERROR) { return TransferResult::failure(lastSocketCode()); }
            const bool is_closed = received == 0 && buffer.length > 0;
            if (is_closed) { return TransferResult::failure(SocketCode::CLOSED); }
            return TransferResult::success(static_cast<size_t>(received));
        }
    }
}

#endif
