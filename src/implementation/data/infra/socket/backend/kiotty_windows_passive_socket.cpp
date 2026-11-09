#if defined(_WIN32)

#include "data/infra/socket/kiotty_passive_socket.h"

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/socket/backend/kiotty_windows_address.h"

#include <windows.h>
#include <mswsock.h>

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        void       releasePath(KiottyNetTransport transport, SOCKET socket,
                       const KiottyNetAddress& address);
        SocketCode bindAndListen(SOCKET socket, const Address& address, int backlog);
        uint16_t   boundPort(SOCKET socket);
        using PeerResult = Result<KiottyNetConnectionInfo, SocketCode>;

        SocketCode inheritListening(SOCKET accepted, SOCKET listening);
        PeerResult readPeer(SOCKET accepted);
    }

    PassiveSocket::PassiveSocket()
        : _handle(INVALID_SOCKET_HANDLE)
        , _transport(KIOTTY_NET_TRANSPORT_TCP)
        , _port(0)
    {
    }

    PassiveSocket::~PassiveSocket()
    {
        close();
    }

    SocketCode PassiveSocket::open(const KiottyNetAddress& address, int backlog)
    {
        close();
        const AddressResult resolved =
            resolveAddress(address.transport, address, AddressRole::PASSIVE);
        if (!resolved) { return resolved.code; }
        const SOCKET created = createSocket(address.transport);
        if (created == INVALID_SOCKET) { return lastSocketCode(); }
        _handle = static_cast<SocketHandle>(created);
        _transport = address.transport;
        const SocketCode prepared = prepareSocket(created);
        if (prepared != SocketCode::SUCCESS)
        {
            close();
            return prepared;
        }
        releasePath(_transport, created, address);
        const SocketCode listening = bindAndListen(created, resolved.value, backlog);
        if (listening != SocketCode::SUCCESS)
        {
            close();
            return listening;
        }
        if (_transport == KIOTTY_NET_TRANSPORT_TCP)
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
        sockaddr* const address = reinterpret_cast<sockaddr*>(&peer);
        const SOCKET accepted = ::accept(socketOf(_handle), address, &length);
        if (accepted == INVALID_SOCKET) { return ActiveSocketResult::failure(lastSocketCode()); }
        ActiveSocket socket(static_cast<SocketHandle>(accepted), _transport);
        const SocketCode prepared = prepareSocket(accepted);
        if (prepared != SocketCode::SUCCESS) { return ActiveSocketResult::failure(prepared); }
        socket._peer = infoOf(peer);
        return ActiveSocketResult::success(std::move(socket));
    }

    ActiveSocketResult PassiveSocket::prepare()
    {
        if (!valid()) { return ActiveSocketResult::failure(SocketCode::INVALID_ARGUMENT); }
        return ActiveSocket::open(_transport);
    }

    ActiveSocketResult PassiveSocket::accept(ActiveSocket&& prepared)
    {
        ActiveSocket socket(std::move(prepared));
        const bool ready = valid() && socket.valid();
        if (!ready) { return ActiveSocketResult::failure(SocketCode::INVALID_ARGUMENT); }
        const SOCKET accepted = socketOf(socket.handle());
        const SocketCode inherited = inheritListening(accepted, socketOf(_handle));
        if (inherited != SocketCode::SUCCESS) { return ActiveSocketResult::failure(inherited); }
        const PeerResult named = readPeer(accepted);
        if (!named) { return ActiveSocketResult::failure(named.code); }
        socket._peer = named.value;
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
        void releasePath(KiottyNetTransport transport, SOCKET socket,
            const KiottyNetAddress& address)
        {
            if (transport == KIOTTY_NET_TRANSPORT_PIPE)
            {
                ::DeleteFileA(address.host);
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

        SocketCode inheritListening(SOCKET accepted, SOCKET listening)
        {
            const char* option = reinterpret_cast<const char*>(&listening);
            const int updated = ::setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                option, sizeof(listening));
            if (updated != 0) { return lastSocketCode(); }
            return SocketCode::SUCCESS;
        }

        PeerResult readPeer(SOCKET accepted)
        {
            sockaddr_storage address{};
            int length = sizeof(address);
            sockaddr* const named_address = reinterpret_cast<sockaddr*>(&address);
            const int named = ::getpeername(accepted, named_address, &length);
            if (named != 0) { return PeerResult::failure(lastSocketCode()); }
            return PeerResult::success(infoOf(address));
        }
    }
}

#endif
