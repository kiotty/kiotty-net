#if defined(_WIN32)

#include "base/data/infra/socket/kiotty_stop_signal.h"

#include "base/data/infra/error/kiotty_socket_error.h"
#include "base/data/infra/socket/backend/kiotty_windows_address.h"

#include <windows.h>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int DRAIN_BYTES = 64;

        SocketCode bindToItself(SOCKET socket);
    }

    SocketCode StopSignal::open()
    {
        if (_reader != INVALID_SOCKET_HANDLE) { return SocketCode::SUCCESS; }
        const DWORD flags = WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT;
        const SOCKET created = ::WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, flags);
        if (created == INVALID_SOCKET) { return lastSocketCode(); }
        _reader = static_cast<SocketHandle>(created);
        _writer = _reader;
        const SocketCode bound = bindToItself(created);
        const SocketCode prepared = bound == SocketCode::SUCCESS ? prepareSocket(created) : bound;
        if (prepared == SocketCode::SUCCESS) { return SocketCode::SUCCESS; }
        close();
        return prepared;
    }

    void StopSignal::drain() const
    {
        if (_reader == INVALID_SOCKET_HANDLE) { return; }
        char sink[DRAIN_BYTES]{};
        for (;;)
        {
            const int taken = ::recv(socketOf(_reader), sink, DRAIN_BYTES, 0);
            if (taken <= 0) { return; }
        }
    }

    void StopSignal::close()
    {
        if (_reader != INVALID_SOCKET_HANDLE) { ::closesocket(socketOf(_reader)); }
        _reader = INVALID_SOCKET_HANDLE;
        _writer = INVALID_SOCKET_HANDLE;
    }

    void StopSignal::notify() const
    {
        if (_writer == INVALID_SOCKET_HANDLE) { return; }
        const char wake = 1;
        static_cast<void>(::send(socketOf(_writer), &wake, sizeof(wake), 0));
    }

    namespace
    {
        SocketCode bindToItself(SOCKET socket)
        {
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
            local.sin_port = 0;
            const sockaddr* target = reinterpret_cast<const sockaddr*>(&local);
            if (::bind(socket, target, sizeof(local)) != 0) { return lastSocketCode(); }
            int length = sizeof(local);
            sockaddr* named = reinterpret_cast<sockaddr*>(&local);
            if (::getsockname(socket, named, &length) != 0) { return lastSocketCode(); }
            if (::connect(socket, target, sizeof(local)) != 0) { return lastSocketCode(); }
            return SocketCode::SUCCESS;
        }
    }
}

#endif
