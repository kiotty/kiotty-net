#if defined(_WIN32)

#include "base/data/infra/socket/backend/kiotty_windows_address.h"

#include "base/data/infra/error/kiotty_socket_error.h"
#include "base/data/infra/socket/kiotty_stop_signal.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "core/kiotty_text.h"

#include <afunix.h>
#include <windows.h>

#include <climits>
#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        using HostName = FixedString<KIOTTY_NET_MAX_HOST_LENGTH>;

        struct Watch
        {
            SOCKET        socket;
            SOCKET        wake;
            WaitDirection direction;
        };

        struct WaitSlice
        {
            uint32_t milliseconds;
            bool     is_expired;
        };

        struct Readiness
        {
            SocketCode code;
            bool       is_woken;
            bool       is_readable;
        };

        AddressResult unixAddressOf(Span<const char> path);
        AddressResult inetAddressOf(const KiottyNetAddress& address, AddressRole role);
        timeval       timevalOf(uint32_t timeout_milliseconds);
        bool          isStopped(const StopSignal* stop);
        SOCKET        wakeOf(const StopSignal* stop);
        WaitSlice     sliceOf(uint64_t deadline, uint32_t timeout_milliseconds);
        Readiness     selectOnce(const Watch& watch, WaitSlice slice);
    }

    SOCKET socketOf(SocketHandle handle)
    {
        return static_cast<SOCKET>(handle);
    }

    int familyOf(KiottyNetTransport transport)
    {
        if (transport == KIOTTY_NET_TRANSPORT_PIPE) { return AF_UNIX; }
        return AF_INET;
    }

    int clampLength(size_t length)
    {
        if (length > static_cast<size_t>(INT_MAX)) { return INT_MAX; }
        return static_cast<int>(length);
    }

    SOCKET createSocket(KiottyNetTransport transport)
    {
        const int protocol = transport == KIOTTY_NET_TRANSPORT_TCP ? IPPROTO_TCP : 0;
        const DWORD flags = WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT;
        return ::WSASocketW(familyOf(transport), SOCK_STREAM, protocol, nullptr, 0, flags);
    }

    SocketCode prepareSocket(SOCKET socket)
    {
        u_long on = 1;
        const int changed = ::ioctlsocket(socket, FIONBIO, &on);
        if (changed != 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
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
            info.port = ::ntohs(inet_address->sin_port);
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

    SocketCode waitReady(SOCKET socket, WaitDirection direction, uint32_t timeout_milliseconds,
        const StopSignal* stop)
    {
        const uint64_t deadline = monotonicMilliseconds() + timeout_milliseconds;
        for (;;)
        {
            if (isStopped(stop)) { return SocketCode::CANCELLED; }
            const WaitSlice slice = sliceOf(deadline, timeout_milliseconds);
            if (slice.is_expired) { return SocketCode::TIMEOUT; }
            const Readiness ready = selectOnce(Watch{ socket, wakeOf(stop), direction }, slice);
            if (ready.code != SocketCode::SUCCESS) { return ready.code; }
            if (!ready.is_woken)
            {
                const bool is_arrival = direction == WaitDirection::EITHER && ready.is_readable;
                return is_arrival ? SocketCode::READABLE : SocketCode::SUCCESS;
            }
            stop->drain();
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
            native.length = static_cast<int>(sizeof(sockaddr_un));
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
            native.length = static_cast<int>(found->ai_addrlen);
            ::freeaddrinfo(found);
            if (!copied) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            sockaddr_in* inet_address = reinterpret_cast<sockaddr_in*>(&native.storage);
            inet_address->sin_port = ::htons(address.port);
            return AddressResult::success(native);
        }

        timeval timevalOf(uint32_t timeout_milliseconds)
        {
            timeval limit{};
            limit.tv_sec = static_cast<long>(timeout_milliseconds / 1000);
            limit.tv_usec = static_cast<long>((timeout_milliseconds % 1000) * 1000);
            return limit;
        }

        bool isStopped(const StopSignal* stop)
        {
            return stop != nullptr && stop->isRaised();
        }

        SOCKET wakeOf(const StopSignal* stop)
        {
            const bool is_watched = stop != nullptr && stop->handle() != INVALID_SOCKET_HANDLE;
            return is_watched ? socketOf(stop->handle()) : INVALID_SOCKET;
        }

        WaitSlice sliceOf(uint64_t deadline, uint32_t timeout_milliseconds)
        {
            if (timeout_milliseconds == 0) { return WaitSlice{ 0, false }; }
            const uint64_t now = monotonicMilliseconds();
            if (now >= deadline) { return WaitSlice{ 0, true }; }
            return WaitSlice{ static_cast<uint32_t>(deadline - now), false };
        }

        Readiness selectOnce(const Watch& watch, WaitSlice slice)
        {
            fd_set readable{};
            fd_set writable{};
            fd_set failed{};
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            const bool is_reading = watch.direction != WaitDirection::WRITE;
            const bool is_writing = watch.direction != WaitDirection::READ;
            if (is_reading) { FD_SET(watch.socket, &readable); }
            if (is_writing) { FD_SET(watch.socket, &writable); }
            FD_SET(watch.socket, &failed);
            if (watch.wake != INVALID_SOCKET) { FD_SET(watch.wake, &readable); }
            timeval limit = timevalOf(slice.milliseconds);
            timeval* wait = slice.milliseconds == 0 ? nullptr : &limit;
            const int found = ::select(0, &readable, &writable, &failed, wait);
            if (found == 0) { return Readiness{ SocketCode::TIMEOUT, false, false }; }
            if (found < 0) { return Readiness{ lastSocketCode(), false, false }; }
            const bool is_woken = watch.wake != INVALID_SOCKET && FD_ISSET(watch.wake, &readable);
            const bool is_readable = FD_ISSET(watch.socket, &readable) != 0;
            const bool is_failed = FD_ISSET(watch.socket, &failed) != 0;
            return Readiness{ SocketCode::SUCCESS, is_woken, is_readable || is_failed };
        }
    }
}

#endif
