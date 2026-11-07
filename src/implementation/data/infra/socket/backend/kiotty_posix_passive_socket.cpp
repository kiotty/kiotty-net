#if !defined(_WIN32)

#include "data/infra/socket/kiotty_passive_socket.h"

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/socket/backend/kiotty_posix_address.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        void       releasePath(KiottyNetTransport transport, int descriptor, const KiottyNetEndpoint& endpoint);
        SocketCode bindAndListen(int descriptor, const Address& address, int backlog);
        uint16_t   boundPort(int descriptor);
        int        acceptOnce(int listening, sockaddr_storage& peer);
    }

    PassiveSocket::PassiveSocket()
        : _handle(INVALID_SOCKET_HANDLE)
        , _transport(KIOTTY_TCP)
        , _port(0)
    {
    }

    PassiveSocket::~PassiveSocket()
    {
        close();
    }

    SocketCode PassiveSocket::open(const KiottyNetEndpoint& endpoint, int backlog)
    {
        close();
        const AddressResult resolved = resolveAddress(endpoint.transport, endpoint, AddressRole::PASSIVE);
        if (!resolved) { return resolved.code; }
        const int descriptor = ::socket(familyOf(endpoint.transport), SOCK_STREAM, 0);
        if (descriptor < 0) { return lastSocketCode(); }
        _handle = descriptor;
        _transport = endpoint.transport;
        const SocketCode prepared = prepareDescriptor(descriptor);
        if (prepared != SocketCode::SUCCESS)
        {
            close();
            return prepared;
        }
        releasePath(_transport, descriptor, endpoint);
        const SocketCode listening = bindAndListen(descriptor, resolved.value, backlog);
        if (listening != SocketCode::SUCCESS)
        {
            close();
            return listening;
        }
        if (_transport == KIOTTY_TCP)
        {
            _port = boundPort(descriptor);
        }
        return SocketCode::SUCCESS;
    }

    ActiveSocketResult PassiveSocket::accept()
    {
        if (!valid()) { return ActiveSocketResult::failure(SocketCode::INVALID_ARGUMENT); }
        sockaddr_storage peer{};
        const int descriptor = acceptOnce(descriptorOf(_handle), peer);
        if (descriptor < 0) { return ActiveSocketResult::failure(lastSocketCode()); }
        ActiveSocket socket(descriptor, _transport);
        const SocketCode prepared = prepareDescriptor(descriptor);
        if (prepared != SocketCode::SUCCESS) { return ActiveSocketResult::failure(prepared); }
        socket._peer = infoOf(peer);
        return ActiveSocketResult::success(std::move(socket));
    }

    void PassiveSocket::close()
    {
        if (!valid()) { return; }
        ::close(descriptorOf(_handle));
        _handle = INVALID_SOCKET_HANDLE;
        _port = 0;
    }

    bool PassiveSocket::valid() const
    {
        return _handle != INVALID_SOCKET_HANDLE;
    }

    SocketHandle PassiveSocket::handle() const
    {
        return _handle;
    }

    uint16_t PassiveSocket::port() const
    {
        return _port;
    }

    namespace
    {
        void releasePath(KiottyNetTransport transport, int descriptor, const KiottyNetEndpoint& endpoint)
        {
            if (transport == KIOTTY_PIPE)
            {
                ::unlink(endpoint.host);
                return;
            }
            const int reuse = 1;
            ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        }

        SocketCode bindAndListen(int descriptor, const Address& address, int backlog)
        {
            const sockaddr* local = reinterpret_cast<const sockaddr*>(&address.storage);
            const int bound = ::bind(descriptor, local, address.length);
            if (bound < 0) { return lastSocketCode(); }
            const int listening = ::listen(descriptor, backlog);
            if (listening < 0) { return lastSocketCode(); }
            return SocketCode::SUCCESS;
        }

        uint16_t boundPort(int descriptor)
        {
            sockaddr_storage bound{};
            socklen_t length = sizeof(bound);
            ::getsockname(descriptor, reinterpret_cast<sockaddr*>(&bound), &length);
            const KiottyNetConnectionInfo info = infoOf(bound);
            return static_cast<uint16_t>(info.port);
        }

        int acceptOnce(int listening, sockaddr_storage& peer)
        {
            socklen_t length = sizeof(peer);
            for (;;)
            {
                const int descriptor = ::accept(listening, reinterpret_cast<sockaddr*>(&peer), &length);
                if (descriptor >= 0 || errno != EINTR) { return descriptor; }
            }
        }
    }
}

#endif
