#if defined(_WIN32)

#include "data/infra/socket/kiotty_passive_socket.h"

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/socket/backend/kiotty_windows_address.h"

#include <windows.h>

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        void       releasePath(KiottyNetTransport transport, SOCKET socket, const KiottyNetEndpoint& endpoint);
        SocketCode bindAndListen(SOCKET socket, const Address& address, int backlog);
        uint16_t   boundPort(SOCKET socket);
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
        const SOCKET created = createSocket(endpoint.transport);
        if (created == INVALID_SOCKET) { return lastSocketCode(); }
        _handle = static_cast<SocketHandle>(created);
        _transport = endpoint.transport;
        const SocketCode prepared = prepareSocket(created);
        if (prepared != SocketCode::SUCCESS)
        {
            close();
            return prepared;
        }
        releasePath(_transport, created, endpoint);
        const SocketCode listening = bindAndListen(created, resolved.value, backlog);
        if (listening != SocketCode::SUCCESS)
        {
            close();
            return listening;
        }
        if (_transport == KIOTTY_TCP)
        {
            _port = boundPort(created);
        }
        return SocketCode::SUCCESS;
    }

    ActiveSocketResult PassiveSocket::accept()
    {
        if (!valid()) { return ActiveSocketResult::failure(SocketCode::INVALID_ARGUMENT); }
        sockaddr_storage peer{};
        int length = sizeof(peer);
        const SOCKET accepted = ::accept(socketOf(_handle), reinterpret_cast<sockaddr*>(&peer), &length);
        if (accepted == INVALID_SOCKET) { return ActiveSocketResult::failure(lastSocketCode()); }
        ActiveSocket socket(static_cast<SocketHandle>(accepted), _transport);
        const SocketCode prepared = prepareSocket(accepted);
        if (prepared != SocketCode::SUCCESS) { return ActiveSocketResult::failure(prepared); }
        socket._peer = infoOf(peer);
        return ActiveSocketResult::success(std::move(socket));
    }

    void PassiveSocket::close()
    {
        if (!valid()) { return; }
        ::closesocket(socketOf(_handle));
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
        void releasePath(KiottyNetTransport transport, SOCKET socket, const KiottyNetEndpoint& endpoint)
        {
            if (transport == KIOTTY_PIPE)
            {
                ::DeleteFileA(endpoint.host);
                return;
            }
            const BOOL exclusive = TRUE;
            const char* option = reinterpret_cast<const char*>(&exclusive);
            ::setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, option, sizeof(exclusive));
        }

        SocketCode bindAndListen(SOCKET socket, const Address& address, int backlog)
        {
            const sockaddr* local = reinterpret_cast<const sockaddr*>(&address.storage);
            const int bound = ::bind(socket, local, address.length);
            if (bound != 0) { return lastSocketCode(); }
            const int listening = ::listen(socket, backlog);
            if (listening != 0) { return lastSocketCode(); }
            return SocketCode::SUCCESS;
        }

        uint16_t boundPort(SOCKET socket)
        {
            sockaddr_storage bound{};
            int length = sizeof(bound);
            ::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length);
            const KiottyNetConnectionInfo info = infoOf(bound);
            return static_cast<uint16_t>(info.port);
        }
    }
}

#endif
