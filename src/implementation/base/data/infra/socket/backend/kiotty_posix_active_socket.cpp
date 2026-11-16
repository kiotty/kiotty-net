#if !defined(_WIN32)

#include "base/data/infra/socket/kiotty_active_socket.h"

#include "base/data/infra/error/kiotty_socket_error.h"
#include "base/data/infra/socket/backend/kiotty_posix_address.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        SocketCode pendingError(int descriptor);
        SocketCode startConnect(int descriptor, const Address& address,
            uint32_t timeout_milliseconds, const StopSignal* stop);
    }

    ActiveSocket::ActiveSocket()
        : _peer()
        , _stop(nullptr)
        , _handle(INVALID_SOCKET_HANDLE)
        , _transport(KIOTTY_NET_TRANSPORT_TCP)
    {
    }

    ActiveSocket::ActiveSocket(SocketHandle handle, KiottyNetTransport transport)
        : _peer()
        , _stop(nullptr)
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
        , _stop(other._stop)
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
        _stop = other._stop;
        _handle = other._handle;
        _transport = other._transport;
        other._handle = INVALID_SOCKET_HANDLE;
        return *this;
    }

    ActiveSocketResult ActiveSocket::open(KiottyNetTransport transport)
    {
        return open(transport, nullptr);
    }

    ActiveSocketResult ActiveSocket::open(KiottyNetTransport transport, const StopSignal* stop)
    {
        const int descriptor = ::socket(familyOf(transport), SOCK_STREAM, 0);
        if (descriptor < 0) { return ActiveSocketResult::failure(lastSocketCode()); }
        ActiveSocket socket(descriptor, transport);
        socket._stop = stop;
        const SocketCode prepared = prepareDescriptor(descriptor);
        if (prepared != SocketCode::SUCCESS) { return ActiveSocketResult::failure(prepared); }
        return ActiveSocketResult::success(std::move(socket));
    }

    SocketCode ActiveSocket::connect(const KiottyNetAddress& address,
        uint32_t timeout_milliseconds)
    {
        if (!valid()) { return SocketCode::INVALID_ARGUMENT; }
        const AddressResult resolved = resolveAddress(_transport, address, AddressRole::ACTIVE);
        if (!resolved) { return resolved.code; }
        const int descriptor = descriptorOf(_handle);
        const SocketCode connected =
            startConnect(descriptor, resolved.value, timeout_milliseconds, _stop);
        if (connected != SocketCode::SUCCESS) { return connected; }
        _peer = infoOf(resolved.value.storage);
        return SocketCode::SUCCESS;
    }

    void ActiveSocket::close()
    {
        if (!valid()) { return; }
        ::close(descriptorOf(_handle));
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

    SocketTraits ActiveSocket::traits() const
    {
        return SocketTraits{ _stop, _transport };
    }

    KiottyNetConnectionInfo ActiveSocket::peer() const
    {
        return _peer;
    }

    namespace
    {
        SocketCode pendingError(int descriptor)
        {
            int error = 0;
            socklen_t length = sizeof(error);
            const int fetched = ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &length);
            if (fetched < 0) { return lastSocketCode(); }
            return toSocketCode(error);
        }

        SocketCode startConnect(int descriptor, const Address& address,
            uint32_t timeout_milliseconds, const StopSignal* stop)
        {
            const sockaddr* target = reinterpret_cast<const sockaddr*>(&address.storage);
            const int connected = ::connect(descriptor, target, address.length);
            if (connected == 0) { return SocketCode::SUCCESS; }
            const int started = errno;
            if (started != EINPROGRESS) { return toSocketCode(started); }
            const SocketCode waited = waitFor(descriptor, POLLOUT, timeout_milliseconds, stop);
            if (waited != SocketCode::SUCCESS) { return waited; }
            return pendingError(descriptor);
        }
    }
}

#endif
