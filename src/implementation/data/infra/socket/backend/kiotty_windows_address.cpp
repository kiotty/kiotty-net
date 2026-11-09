#if defined(_WIN32)

#include "data/infra/socket/backend/kiotty_windows_address.h"

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "core/kiotty_text.h"
#include "data/infra/error/kiotty_socket_error.h"

#include <afunix.h>
#include <windows.h>

#include <climits>
#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        using HostName = FixedString<KIOTTY_NET_MAX_HOST_LENGTH>;

        AddressResult unixAddressOf(Span<const char> path);
        AddressResult inetAddressOf(const KiottyNetAddress& address, AddressRole role);
        timeval       timevalOf(uint32_t timeout_milliseconds);
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

    SocketCode waitReady(SOCKET socket, WaitDirection direction, uint32_t timeout_milliseconds)
    {
        fd_set ready{};
        fd_set failed{};
        FD_ZERO(&ready);
        FD_ZERO(&failed);
        FD_SET(socket, &ready);
        FD_SET(socket, &failed);
        timeval limit = timevalOf(timeout_milliseconds);
        timeval* wait = timeout_milliseconds == 0 ? nullptr : &limit;
        fd_set* readable = direction == WaitDirection::READ ? &ready : nullptr;
        fd_set* writable = direction == WaitDirection::WRITE ? &ready : nullptr;
        const int found = ::select(0, readable, writable, &failed, wait);
        if (found == 0) { return SocketCode::TIMEOUT; }
        if (found < 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
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
    }
}

#endif
