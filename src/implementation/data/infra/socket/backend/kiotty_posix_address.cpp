#if !defined(_WIN32)

#include "data/infra/socket/backend/kiotty_posix_address.h"

#include "core/kiotty_clock.h"
#include "core/kiotty_span.h"
#include "data/infra/error/kiotty_socket_error.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/un.h>

#include <cerrno>
#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        AddressResult unixAddressOf(const char* path);
        AddressResult inetAddressOf(const KiottyNetEndpoint& endpoint, AddressRole role);
        int           waitMillisecondsOf(uint64_t deadline, uint32_t timeout_milliseconds);
        SocketCode    disableSigpipe(int descriptor);
    }

    int descriptorOf(SocketHandle handle)
    {
        return static_cast<int>(handle);
    }

    int familyOf(KiottyNetTransport transport)
    {
        if (transport == KIOTTY_NET_TRANSPORT_PIPE) { return AF_UNIX; }
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

    SocketCode prepareDescriptor(int descriptor)
    {
        const int flags = ::fcntl(descriptor, F_GETFL, 0);
        if (flags < 0) { return lastSocketCode(); }
        const int non_blocking = ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
        if (non_blocking < 0) { return lastSocketCode(); }
        const int close_on_exec = ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
        if (close_on_exec < 0) { return lastSocketCode(); }
        return disableSigpipe(descriptor);
    }

    AddressResult resolveAddress(KiottyNetTransport transport, const KiottyNetEndpoint& endpoint,
        AddressRole role)
    {
        if (transport == KIOTTY_NET_TRANSPORT_PIPE) { return unixAddressOf(endpoint.host); }
        return inetAddressOf(endpoint, role);
    }

    KiottyNetConnectionInfo infoOf(const sockaddr_storage& storage)
    {
        KiottyNetConnectionInfo info{};
        if (storage.ss_family == AF_INET)
        {
            const sockaddr_in* inet_address = reinterpret_cast<const sockaddr_in*>(&storage);
            ::inet_ntop(AF_INET, &inet_address->sin_addr, info.hostname, sizeof(info.hostname));
            info.port = ntohs(inet_address->sin_port);
        }
        if (storage.ss_family == AF_UNIX)
        {
            const sockaddr_un* unix_address = reinterpret_cast<const sockaddr_un*>(&storage);
            const size_t length = strnlen(unix_address->sun_path, sizeof(unix_address->sun_path));
            const Span<char> target{ info.hostname, sizeof(info.hostname) - 1 };
            const Span<const char> source{ unix_address->sun_path, length };
            static_cast<void>(target.copyFrom(source));
        }
        return info;
    }

    SocketCode waitFor(int descriptor, short events, uint32_t timeout_milliseconds)
    {
        const uint64_t deadline = monotonicMilliseconds() + timeout_milliseconds;
        for (;;)
        {
            const int wait_milliseconds = waitMillisecondsOf(deadline, timeout_milliseconds);
            pollfd entry{};
            entry.fd = descriptor;
            entry.events = events;
            const int ready = ::poll(&entry, 1, wait_milliseconds);
            if (ready > 0) { return SocketCode::SUCCESS; }
            if (ready == 0) { return SocketCode::TIMEOUT; }
            if (errno != EINTR) { return lastSocketCode(); }
        }
    }

    namespace
    {
        AddressResult unixAddressOf(const char* path)
        {
            Address address{};
            sockaddr_un* unix_address = reinterpret_cast<sockaddr_un*>(&address.storage);
            const size_t length = strnlen(path, sizeof(unix_address->sun_path));
            const bool is_fitting = length > 0 && length < sizeof(unix_address->sun_path);
            if (!is_fitting) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            unix_address->sun_family = AF_UNIX;
            const Span<char> target{ unix_address->sun_path, sizeof(unix_address->sun_path) };
            const Span<const char> source{ path, length };
            static_cast<void>(target.copyFrom(source));
            address.length = static_cast<socklen_t>(sizeof(sockaddr_un));
            return AddressResult::success(address);
        }

        AddressResult inetAddressOf(const KiottyNetEndpoint& endpoint, AddressRole role)
        {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = role == AddressRole::PASSIVE ? AI_PASSIVE : 0;
            const char* host = endpoint.host[0] == '\0' ? nullptr : endpoint.host;
            addrinfo* found = nullptr;
            const int resolved = ::getaddrinfo(host, nullptr, &hints, &found);
            const bool is_found = resolved == 0 && found != nullptr;
            if (!is_found) { return AddressResult::failure(SocketCode::UNREACHABLE); }
            Address address{};
            char* const storage = reinterpret_cast<char*>(&address.storage);
            const char* const found_address = reinterpret_cast<const char*>(found->ai_addr);
            const Span<char> target{ storage, sizeof(address.storage) };
            const Span<const char> source{ found_address, found->ai_addrlen };
            const bool copied = target.copyFrom(source);
            address.length = static_cast<socklen_t>(found->ai_addrlen);
            ::freeaddrinfo(found);
            if (!copied) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            sockaddr_in* inet_address = reinterpret_cast<sockaddr_in*>(&address.storage);
            inet_address->sin_port = htons(endpoint.port);
            return AddressResult::success(address);
        }

        int waitMillisecondsOf(uint64_t deadline, uint32_t timeout_milliseconds)
        {
            if (timeout_milliseconds == 0) { return -1; }
            const uint64_t now = monotonicMilliseconds();
            const uint64_t remaining = deadline > now ? deadline - now : 0;
            return static_cast<int>(remaining);
        }

        SocketCode disableSigpipe(int descriptor)
        {
#if defined(SO_NOSIGPIPE)
            const int on = 1;
            const int set = ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
            if (set < 0) { return lastSocketCode(); }
#else
            static_cast<void>(descriptor);
#endif
            return SocketCode::SUCCESS;
        }
    }
}

#endif
