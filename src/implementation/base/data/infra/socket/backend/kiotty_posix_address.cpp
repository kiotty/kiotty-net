#if !defined(_WIN32)

#include "base/data/infra/socket/backend/kiotty_posix_address.h"

#include "base/data/infra/error/kiotty_socket_error.h"
#include "base/data/infra/socket/kiotty_stop_signal.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "core/kiotty_text.h"

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
        using HostName = FixedString<KIOTTY_NET_MAX_HOST_LENGTH>;

        AddressResult unixAddressOf(Span<const char> path);
        AddressResult inetAddressOf(const KiottyNetAddress& address, AddressRole role);
        constexpr nfds_t WAIT_ENTRIES = 2;

        int           waitMillisecondsOf(uint64_t deadline, uint32_t timeout_milliseconds);
        SocketCode    disableSigpipe(int descriptor);
        bool          isStopped(const StopSignal* stop);
        pollfd        entryOf(int descriptor, short events);
        pollfd        stopEntryOf(const StopSignal* stop);
        SocketCode    readinessOf(const pollfd& entry);
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

    AddressResult resolveAddress(KiottyNetTransport transport, const KiottyNetAddress& address,
        AddressRole role)
    {
        if (transport == KIOTTY_NET_TRANSPORT_PIPE) { return unixAddressOf(textOf(address.host)); }
        return inetAddressOf(address, role);
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

    SocketCode waitFor(int descriptor, short events, uint32_t timeout_milliseconds,
        const StopSignal* stop)
    {
        const uint64_t deadline = monotonicMilliseconds() + timeout_milliseconds;
        for (;;)
        {
            if (isStopped(stop)) { return SocketCode::CANCELLED; }
            const int wait_milliseconds = waitMillisecondsOf(deadline, timeout_milliseconds);
            pollfd entries[WAIT_ENTRIES] = { entryOf(descriptor, events), stopEntryOf(stop) };
            const int ready = ::poll(entries, WAIT_ENTRIES, wait_milliseconds);
            if (ready == 0) { return SocketCode::TIMEOUT; }
            if (ready < 0 && errno != EINTR) { return lastSocketCode(); }
            if (entries[1].revents != 0)
            {
                stop->drain();
            }
            else if (ready > 0)
            {
                return readinessOf(entries[0]);
            }
        }
    }

    namespace
    {
        AddressResult unixAddressOf(Span<const char> path)
        {
            Address native{};
            sockaddr_un* unix_address = reinterpret_cast<sockaddr_un*>(&native.storage);
            const size_t room = sizeof(unix_address->sun_path);
            const bool is_fitting = !path.empty() && path.length < room;
            if (!is_fitting) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            unix_address->sun_family = AF_UNIX;
            const Span<char> target{ unix_address->sun_path, room };
            static_cast<void>(target.copyFrom(path));
            native.length = static_cast<socklen_t>(sizeof(sockaddr_un));
            return AddressResult::success(native);
        }

        AddressResult inetAddressOf(const KiottyNetAddress& address, AddressRole role)
        {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = role == AddressRole::PASSIVE ? AI_PASSIVE : 0;
            HostName name;
            const bool is_assigned = name.assign(textOf(address.host));
            if (!is_assigned) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            const char* host = name.empty() ? nullptr : name.c_str();
            addrinfo* found = nullptr;
            const int resolved = ::getaddrinfo(host, nullptr, &hints, &found);
            const bool is_found = resolved == 0 && found != nullptr;
            if (!is_found) { return AddressResult::failure(SocketCode::UNREACHABLE); }
            Address native{};
            char* const storage = reinterpret_cast<char*>(&native.storage);
            const char* const found_address = reinterpret_cast<const char*>(found->ai_addr);
            const Span<char> target{ storage, sizeof(native.storage) };
            const Span<const char> source{ found_address, found->ai_addrlen };
            const bool copied = target.copyFrom(source);
            native.length = static_cast<socklen_t>(found->ai_addrlen);
            ::freeaddrinfo(found);
            if (!copied) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            sockaddr_in* inet_address = reinterpret_cast<sockaddr_in*>(&native.storage);
            inet_address->sin_port = htons(address.port);
            return AddressResult::success(native);
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

        bool isStopped(const StopSignal* stop)
        {
            return stop != nullptr && stop->isRaised();
        }

        SocketCode readinessOf(const pollfd& entry)
        {
            const short both = POLLIN | POLLOUT;
            const bool is_either = (entry.events & both) == both;
            const short arrival = POLLIN | POLLHUP | POLLERR;
            const bool is_readable = (entry.revents & arrival) != 0;
            return is_either && is_readable ? SocketCode::READABLE : SocketCode::SUCCESS;
        }

        pollfd entryOf(int descriptor, short events)
        {
            pollfd entry{};
            entry.fd = descriptor;
            entry.events = events;
            return entry;
        }

        pollfd stopEntryOf(const StopSignal* stop)
        {
            const bool is_watched = stop != nullptr && stop->handle() != INVALID_SOCKET_HANDLE;
            return entryOf(is_watched ? descriptorOf(stop->handle()) : -1, POLLIN);
        }
    }
}

#endif
