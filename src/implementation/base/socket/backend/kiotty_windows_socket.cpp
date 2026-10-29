#include "base/socket/kiotty_socket.h"
#include "core/kiotty_clock.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>

#include <climits>
#include <cstring>
#include <utility>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            struct Address
            {
                sockaddr_storage storage;
                int              length;
            };

            SOCKET socketOf(SocketHandle handle)
            {
                return static_cast<SOCKET>(handle);
            }

            int familyOf(KiottyNetTransport transport)
            {
                if (transport == KIOTTY_PIPE)
                {
                    return AF_UNIX;
                }
                return AF_INET;
            }

            int clampLength(size_t length)
            {
                if (length > static_cast<size_t>(INT_MAX))
                {
                    return INT_MAX;
                }
                return static_cast<int>(length);
            }

            SOCKET createSocket(KiottyNetTransport transport)
            {
                int protocol = 0;
                if (transport == KIOTTY_TCP)
                {
                    protocol = IPPROTO_TCP;
                }
                DWORD flags = WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT;
                return ::WSASocketW(familyOf(transport), SOCK_STREAM, protocol, nullptr, 0, flags);
            }

            SocketCode prepare(SOCKET socket)
            {
                u_long on = 1;
                if (::ioctlsocket(socket, FIONBIO, &on) != 0)
                {
                    return lastSocketCode();
                }
                return SocketCode::SUCCESS;
            }

            SocketCode unixAddressOf(const char* path, Address& address)
            {
                sockaddr_un* unix_address = reinterpret_cast<sockaddr_un*>(&address.storage);
                size_t length = std::strlen(path);
                if (length == 0 || length >= sizeof(unix_address->sun_path))
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                std::memset(&address.storage, 0, sizeof(address.storage));
                unix_address->sun_family = AF_UNIX;
                std::memcpy(unix_address->sun_path, path, length);
                address.length = static_cast<int>(sizeof(sockaddr_un));
                return SocketCode::SUCCESS;
            }

            SocketCode inetAddressOf(const KiottyNetEndpoint& endpoint, bool passive, Address& address)
            {
                addrinfo hints;
                std::memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (passive)
                {
                    hints.ai_flags = AI_PASSIVE;
                }
                const char* host = endpoint.host;
                if (host[0] == '\0')
                {
                    host = nullptr;
                }
                addrinfo* found = nullptr;
                int resolved = ::getaddrinfo(host, nullptr, &hints, &found);
                if (resolved != 0 || found == nullptr)
                {
                    return SocketCode::UNREACHABLE;
                }
                std::memset(&address.storage, 0, sizeof(address.storage));
                std::memcpy(&address.storage, found->ai_addr, found->ai_addrlen);
                address.length = static_cast<int>(found->ai_addrlen);
                ::freeaddrinfo(found);
                sockaddr_in* inet_address = reinterpret_cast<sockaddr_in*>(&address.storage);
                inet_address->sin_port = ::htons(endpoint.port);
                return SocketCode::SUCCESS;
            }

            SocketCode addressOf(KiottyNetTransport transport, const KiottyNetEndpoint& endpoint, bool passive, Address& address)
            {
                if (transport == KIOTTY_PIPE)
                {
                    return unixAddressOf(endpoint.host, address);
                }
                return inetAddressOf(endpoint, passive, address);
            }

            KiottyNetConnectionInfo infoOf(const sockaddr_storage& storage)
            {
                KiottyNetConnectionInfo info;
                std::memset(&info, 0, sizeof(info));
                if (storage.ss_family == AF_INET)
                {
                    const sockaddr_in* inet_address = reinterpret_cast<const sockaddr_in*>(&storage);
                    ::inet_ntop(AF_INET, &inet_address->sin_addr, info.hostname, sizeof(info.hostname));
                    info.port = ::ntohs(inet_address->sin_port);
                }
                if (storage.ss_family == AF_UNIX)
                {
                    const sockaddr_un* unix_address = reinterpret_cast<const sockaddr_un*>(&storage);
                    size_t length = strnlen(unix_address->sun_path, sizeof(unix_address->sun_path));
                    std::memcpy(info.hostname, unix_address->sun_path, length);
                }
                return info;
            }

            SocketCode waitConnected(SOCKET socket, uint32_t timeout_ms)
            {
                fd_set writable;
                fd_set failed;
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET(socket, &writable);
                FD_SET(socket, &failed);
                timeval limit;
                limit.tv_sec = static_cast<long>(timeout_ms / 1000);
                limit.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
                timeval* wait = &limit;
                if (timeout_ms == 0)
                {
                    wait = nullptr;
                }
                int ready = ::select(0, nullptr, &writable, &failed, wait);
                if (ready == 0)
                {
                    return SocketCode::TIMEOUT;
                }
                if (ready < 0)
                {
                    return lastSocketCode();
                }
                int error = 0;
                int length = sizeof(error);
                if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0)
                {
                    return lastSocketCode();
                }
                return toSocketCode(error);
            }
        }

        ActiveSocket::ActiveSocket()
            : _handle(INVALID_SOCKET_HANDLE)
            , _transport(KIOTTY_TCP)
            , _peer()
        {
        }

        ActiveSocket::ActiveSocket(SocketHandle handle, KiottyNetTransport transport)
            : _handle(handle)
            , _transport(transport)
            , _peer()
        {
        }

        ActiveSocket::~ActiveSocket()
        {
            close();
        }

        ActiveSocket::ActiveSocket(ActiveSocket&& other) noexcept
            : _handle(other._handle)
            , _transport(other._transport)
            , _peer(other._peer)
        {
            other._handle = INVALID_SOCKET_HANDLE;
        }

        ActiveSocket& ActiveSocket::operator=(ActiveSocket&& other) noexcept
        {
            if (this != &other)
            {
                close();
                _handle = other._handle;
                _transport = other._transport;
                _peer = other._peer;
                other._handle = INVALID_SOCKET_HANDLE;
            }
            return *this;
        }

        ActiveSocketResult ActiveSocket::open(KiottyNetTransport transport)
        {
            SOCKET created = createSocket(transport);
            if (created == INVALID_SOCKET)
            {
                return error(lastSocketCode());
            }
            ActiveSocket socket(static_cast<SocketHandle>(created), transport);
            SocketCode prepared = prepare(created);
            if (prepared != SocketCode::SUCCESS)
            {
                return error(prepared);
            }
            return ActiveSocketResult(ActiveSocketResult::Success(), std::move(socket));
        }

        SocketCode ActiveSocket::connect(const KiottyNetEndpoint& endpoint, uint32_t timeout_ms)
        {
            if (!valid())
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            Address address;
            SocketCode resolved = addressOf(_transport, endpoint, false, address);
            if (resolved != SocketCode::SUCCESS)
            {
                return resolved;
            }
            SOCKET socket = socketOf(_handle);
            const sockaddr* target = reinterpret_cast<const sockaddr*>(&address.storage);
            if (::connect(socket, target, address.length) != 0)
            {
                int started = ::WSAGetLastError();
                if (started != WSAEWOULDBLOCK)
                {
                    return toSocketCode(started);
                }
                SocketCode connected = waitConnected(socket, timeout_ms);
                if (connected != SocketCode::SUCCESS)
                {
                    return connected;
                }
            }
            _peer = infoOf(address.storage);
            return SocketCode::SUCCESS;
        }

        SocketCode ActiveSocket::setNoDelay(bool on)
        {
            if (!valid())
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            if (_transport == KIOTTY_PIPE)
            {
                return SocketCode::SUCCESS;
            }
            BOOL value = on ? TRUE : FALSE;
            const char* option = reinterpret_cast<const char*>(&value);
            if (::setsockopt(socketOf(_handle), IPPROTO_TCP, TCP_NODELAY, option, sizeof(value)) != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        TransferResult ActiveSocket::send(const void* data, size_t length)
        {
            if (!valid())
            {
                return error(SocketCode::INVALID_ARGUMENT);
            }
            const char* bytes = static_cast<const char*>(data);
            int sent = ::send(socketOf(_handle), bytes, clampLength(length), 0);
            if (sent == SOCKET_ERROR)
            {
                return error(lastSocketCode());
            }
            return ok(static_cast<size_t>(sent));
        }

        TransferResult ActiveSocket::receive(void* data, size_t length)
        {
            if (!valid())
            {
                return error(SocketCode::INVALID_ARGUMENT);
            }
            char* bytes = static_cast<char*>(data);
            int received = ::recv(socketOf(_handle), bytes, clampLength(length), 0);
            if (received == SOCKET_ERROR)
            {
                return error(lastSocketCode());
            }
            if (received == 0 && length > 0)
            {
                return error(SocketCode::CLOSED);
            }
            return ok(static_cast<size_t>(received));
        }

        SocketCode ActiveSocket::shutdown()
        {
            if (!valid())
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            if (::shutdown(socketOf(_handle), SD_SEND) != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        void ActiveSocket::close()
        {
            if (!valid())
            {
                return;
            }
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

        KiottyNetConnectionInfo ActiveSocket::peer() const
        {
            return _peer;
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
            Address address;
            SocketCode resolved = addressOf(endpoint.transport, endpoint, true, address);
            if (resolved != SocketCode::SUCCESS)
            {
                return resolved;
            }
            SOCKET created = createSocket(endpoint.transport);
            if (created == INVALID_SOCKET)
            {
                return lastSocketCode();
            }
            _handle = static_cast<SocketHandle>(created);
            _transport = endpoint.transport;
            SocketCode prepared = prepare(created);
            if (prepared != SocketCode::SUCCESS)
            {
                close();
                return prepared;
            }
            if (_transport == KIOTTY_PIPE)
            {
                ::DeleteFileA(endpoint.host);
            }
            else
            {
                BOOL exclusive = TRUE;
                const char* option = reinterpret_cast<const char*>(&exclusive);
                ::setsockopt(created, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, option, sizeof(exclusive));
            }
            const sockaddr* local = reinterpret_cast<const sockaddr*>(&address.storage);
            if (::bind(created, local, address.length) != 0)
            {
                SocketCode code = lastSocketCode();
                close();
                return code;
            }
            if (::listen(created, backlog) != 0)
            {
                SocketCode code = lastSocketCode();
                close();
                return code;
            }
            if (_transport == KIOTTY_TCP)
            {
                sockaddr_storage bound;
                int length = sizeof(bound);
                std::memset(&bound, 0, sizeof(bound));
                ::getsockname(created, reinterpret_cast<sockaddr*>(&bound), &length);
                KiottyNetConnectionInfo info = infoOf(bound);
                _port = static_cast<uint16_t>(info.port);
            }
            return SocketCode::SUCCESS;
        }

        ActiveSocketResult PassiveSocket::accept()
        {
            if (!valid())
            {
                return error(SocketCode::INVALID_ARGUMENT);
            }
            sockaddr_storage peer;
            int length = sizeof(peer);
            std::memset(&peer, 0, sizeof(peer));
            SOCKET accepted = ::accept(socketOf(_handle), reinterpret_cast<sockaddr*>(&peer), &length);
            if (accepted == INVALID_SOCKET)
            {
                return error(lastSocketCode());
            }
            ActiveSocket socket(static_cast<SocketHandle>(accepted), _transport);
            SocketCode prepared = prepare(accepted);
            if (prepared != SocketCode::SUCCESS)
            {
                return error(prepared);
            }
            socket._peer = infoOf(peer);
            return ActiveSocketResult(ActiveSocketResult::Success(), std::move(socket));
        }

        void PassiveSocket::close()
        {
            if (!valid())
            {
                return;
            }
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
    }
}
