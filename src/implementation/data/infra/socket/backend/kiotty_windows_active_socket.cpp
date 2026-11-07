#if defined(_WIN32)

#include "data/infra/socket/kiotty_active_socket.h"

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/socket/backend/kiotty_windows_address.h"

#include <windows.h>

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        SocketCode pendingError(SOCKET socket);
        SocketCode startConnect(SOCKET socket, const Address& address,
            uint32_t timeout_milliseconds);
    }

    ActiveSocket::ActiveSocket()
        : _peer()
        , _handle(INVALID_SOCKET_HANDLE)
        , _transport(KIOTTY_NET_TRANSPORT_TCP)
    {
    }

    ActiveSocket::ActiveSocket(SocketHandle handle, KiottyNetTransport transport)
        : _peer()
        , _handle(handle)
        , _transport(transport)
    {
    }

    ActiveSocket::~ActiveSocket()
    {
        close();
    }

    ActiveSocket::ActiveSocket(ActiveSocket&& other) noexcept
        : _peer(other._peer)
        , _handle(other._handle)
        , _transport(other._transport)
    {
        other._handle = INVALID_SOCKET_HANDLE;
    }

    ActiveSocket& ActiveSocket::operator=(ActiveSocket&& other) noexcept
    {
        if (this == &other) { return *this; }
        close();
        _peer = other._peer;
        _handle = other._handle;
        _transport = other._transport;
        other._handle = INVALID_SOCKET_HANDLE;
        return *this;
    }

    ActiveSocketResult ActiveSocket::open(KiottyNetTransport transport)
    {
        const SOCKET created = createSocket(transport);
        if (created == INVALID_SOCKET) { return ActiveSocketResult::failure(lastSocketCode()); }
        ActiveSocket socket(static_cast<SocketHandle>(created), transport);
        const SocketCode prepared = prepareSocket(created);
        if (prepared != SocketCode::SUCCESS) { return ActiveSocketResult::failure(prepared); }
        return ActiveSocketResult::success(std::move(socket));
    }

    SocketCode ActiveSocket::connect(const KiottyNetEndpoint& endpoint,
        uint32_t timeout_milliseconds)
    {
        if (!valid()) { return SocketCode::INVALID_ARGUMENT; }
        const AddressResult resolved = resolveAddress(_transport, endpoint, AddressRole::ACTIVE);
        if (!resolved) { return resolved.code; }
        const SOCKET socket = socketOf(_handle);
        const SocketCode connected = startConnect(socket, resolved.value, timeout_milliseconds);
        if (connected != SocketCode::SUCCESS) { return connected; }
        _peer = infoOf(resolved.value.storage);
        return SocketCode::SUCCESS;
    }

    void ActiveSocket::close()
    {
        if (!valid()) { return; }
        ::closesocket(socketOf(_handle));
        _handle = INVALID_SOCKET_HANDLE;
    }

    bool ActiveSocket::valid() const
    {
        return _handle != INVALID_SOCKET_HANDLE;
    }

    SocketHandle ActiveSocket::handle() const
    {
        return _handle;
    }

    KiottyNetTransport ActiveSocket::transport() const
    {
        return _transport;
    }

    KiottyNetConnectionInfo ActiveSocket::peer() const
    {
        return _peer;
    }

    namespace
    {
        SocketCode pendingError(SOCKET socket)
        {
            int error = 0;
            int length = sizeof(error);
            char* const value = reinterpret_cast<char*>(&error);
            const int fetched = ::getsockopt(socket, SOL_SOCKET, SO_ERROR, value, &length);
            if (fetched != 0) { return lastSocketCode(); }
            return toSocketCode(error);
        }

        SocketCode startConnect(SOCKET socket, const Address& address,
            uint32_t timeout_milliseconds)
        {
            const sockaddr* target = reinterpret_cast<const sockaddr*>(&address.storage);
            const int connected = ::connect(socket, target, address.length);
            if (connected == 0) { return SocketCode::SUCCESS; }
            const int started = ::WSAGetLastError();
            if (started != WSAEWOULDBLOCK) { return toSocketCode(started); }
            const SocketCode waited = waitReady(socket, WaitDirection::WRITE, timeout_milliseconds);
            if (waited != SocketCode::SUCCESS) { return waited; }
            return pendingError(socket);
        }
    }
}

#endif
