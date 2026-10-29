#include "base/socket/kiotty_socket.h"
#include "core/kiotty_clock.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
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
                socklen_t        length;
            };

            int descriptorOf(SocketHandle handle)
            {
                return static_cast<int>(handle);
            }

            int familyOf(KiottyNetTransport transport)
            {
                if (transport == KIOTTY_PIPE)
                {
                    return AF_UNIX;
                }
                return AF_INET;
            }

            int sendFlags()
            {
#if defined(MSG_NOSIGNAL)
                return MSG_NOSIGNAL;
#else
                return 0;
#endif
            }

            SocketCode prepare(int descriptor)
            {
                int flags = ::fcntl(descriptor, F_GETFL, 0);
                if (flags < 0)
                {
                    return lastSocketCode();
                }
                if (::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0)
                {
                    return lastSocketCode();
                }
                if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0)
                {
                    return lastSocketCode();
                }
#if defined(SO_NOSIGPIPE)
                int on = 1;
                if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) < 0)
                {
                    return lastSocketCode();
                }
#endif
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
                address.length = static_cast<socklen_t>(sizeof(sockaddr_un));
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
                address.length = static_cast<socklen_t>(found->ai_addrlen);
                ::freeaddrinfo(found);
                sockaddr_in* inet_address = reinterpret_cast<sockaddr_in*>(&address.storage);
                inet_address->sin_port = htons(endpoint.port);
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
                    info.port = ntohs(inet_address->sin_port);
                }
                if (storage.ss_family == AF_UNIX)
                {
                    const sockaddr_un* unix_address = reinterpret_cast<const sockaddr_un*>(&storage);
                    size_t length = strnlen(unix_address->sun_path, sizeof(unix_address->sun_path));
                    std::memcpy(info.hostname, unix_address->sun_path, length);
                }
                return info;
            }

            SocketCode waitWritable(int descriptor, uint32_t timeout_ms)
            {
                uint64_t deadline = monotonicMs() + timeout_ms;
                for (;;)
                {
                    int wait_ms = -1;
                    if (timeout_ms != 0)
                    {
                        uint64_t now = monotonicMs();
                        uint64_t remaining = deadline > now ? deadline - now : 0;
                        wait_ms = static_cast<int>(remaining);
                    }
                    pollfd entry;
                    entry.fd = descriptor;
                    entry.events = POLLOUT;
                    entry.revents = 0;
                    int ready = ::poll(&entry, 1, wait_ms);
                    if (ready > 0)
                    {
                        return SocketCode::SUCCESS;
                    }
                    if (ready == 0)
                    {
                        return SocketCode::TIMEOUT;
                    }
                    if (errno != EINTR)
                    {
                        return lastSocketCode();
                    }
                }
            }

            SocketCode pendingError(int descriptor)
            {
                int error = 0;
                socklen_t length = sizeof(error);
                if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
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
            int descriptor = ::socket(familyOf(transport), SOCK_STREAM, 0);
            if (descriptor < 0)
            {
                return error(lastSocketCode());
            }
            ActiveSocket socket(descriptor, transport);
            SocketCode prepared = prepare(descriptor);
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
            int descriptor = descriptorOf(_handle);
            const sockaddr* target = reinterpret_cast<const sockaddr*>(&address.storage);
            if (::connect(descriptor, target, address.length) != 0)
            {
                int started = errno;
                if (started != EINPROGRESS)
                {
                    return toSocketCode(started);
                }
                SocketCode waited = waitWritable(descriptor, timeout_ms);
                if (waited != SocketCode::SUCCESS)
                {
                    return waited;
                }
                SocketCode connected = pendingError(descriptor);
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
            int value = on ? 1 : 0;
            if (::setsockopt(descriptorOf(_handle), IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) < 0)
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
            for (;;)
            {
                ssize_t sent = ::send(descriptorOf(_handle), data, length, sendFlags());
                if (sent >= 0)
                {
                    return ok(static_cast<size_t>(sent));
                }
                if (errno != EINTR)
                {
                    return error(lastSocketCode());
                }
            }
        }

        TransferResult ActiveSocket::receive(void* data, size_t length)
        {
            if (!valid())
            {
                return error(SocketCode::INVALID_ARGUMENT);
            }
            for (;;)
            {
                ssize_t received = ::recv(descriptorOf(_handle), data, length, 0);
                if (received > 0)
                {
                    return ok(static_cast<size_t>(received));
                }
                if (received == 0 && length > 0)
                {
                    return error(SocketCode::CLOSED);
                }
                if (received == 0)
                {
                    return ok(static_cast<size_t>(0));
                }
                if (errno != EINTR)
                {
                    return error(lastSocketCode());
                }
            }
        }

        SocketCode ActiveSocket::shutdown()
        {
            if (!valid())
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            if (::shutdown(descriptorOf(_handle), SHUT_WR) < 0)
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
            int descriptor = ::socket(familyOf(endpoint.transport), SOCK_STREAM, 0);
            if (descriptor < 0)
            {
                return lastSocketCode();
            }
            _handle = descriptor;
            _transport = endpoint.transport;
            SocketCode prepared = prepare(descriptor);
            if (prepared != SocketCode::SUCCESS)
            {
                close();
                return prepared;
            }
            if (_transport == KIOTTY_PIPE)
            {
                ::unlink(endpoint.host);
            }
            else
            {
                int reuse = 1;
                ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            }
            const sockaddr* local = reinterpret_cast<const sockaddr*>(&address.storage);
            if (::bind(descriptor, local, address.length) < 0)
            {
                SocketCode code = lastSocketCode();
                close();
                return code;
            }
            if (::listen(descriptor, backlog) < 0)
            {
                SocketCode code = lastSocketCode();
                close();
                return code;
            }
            if (_transport == KIOTTY_TCP)
            {
                sockaddr_storage bound;
                socklen_t length = sizeof(bound);
                std::memset(&bound, 0, sizeof(bound));
                ::getsockname(descriptor, reinterpret_cast<sockaddr*>(&bound), &length);
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
            socklen_t length = sizeof(peer);
            std::memset(&peer, 0, sizeof(peer));
            int descriptor = -1;
            for (;;)
            {
                descriptor = ::accept(descriptorOf(_handle), reinterpret_cast<sockaddr*>(&peer), &length);
                if (descriptor >= 0 || errno != EINTR)
                {
                    break;
                }
            }
            if (descriptor < 0)
            {
                return error(lastSocketCode());
            }
            ActiveSocket socket(descriptor, _transport);
            SocketCode prepared = prepare(descriptor);
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
    }
}
