#if defined(_WIN32)

#include "data/infra/socket/backend/kiotty_windows_address.h"

#include "core/kiotty_span.h"
#include "data/infra/error/kiotty_socket_error.h"

#include <afunix.h>
#include <windows.h>

#include <climits>
#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        AddressResult unixAddressOf(const char* path);
        AddressResult inetAddressOf(const KiottyNetEndpoint& endpoint, AddressRole role);
        timeval       timevalOf(uint32_t timeout_milliseconds);
    }

    SOCKET socketOf(SocketHandle handle)
    {
        return static_cast<SOCKET>(handle);
    }

    int familyOf(KiottyNetTransport transport)
    {
        if (transport == KIOTTY_PIPE) { return AF_UNIX; }
        return AF_INET;
    }

    int clampLength(size_t length)
    {
        if (length > static_cast<size_t>(INT_MAX)) { return INT_MAX; }
        return static_cast<int>(length);
    }

    SOCKET createSocket(KiottyNetTransport transport)
    {
        const int protocol = transport == KIOTTY_TCP ? IPPROTO_TCP : 0;
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

    AddressResult resolveAddress(KiottyNetTransport transport, const KiottyNetEndpoint& endpoint, AddressRole role)
    {
        if (transport == KIOTTY_PIPE) { return unixAddressOf(endpoint.host); }
        return inetAddressOf(endpoint, role);
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
        fd_set ready;
        fd_set failed;
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
        AddressResult unixAddressOf(const char* path)
        {
            Address address{};
            sockaddr_un* unix_address = reinterpret_cast<sockaddr_un*>(&address.storage);
            const size_t length = strnlen(path, sizeof(unix_address->sun_path));
            if (length == 0 || length >= sizeof(unix_address->sun_path)) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            unix_address->sun_family = AF_UNIX;
            const Span<char> target{ unix_address->sun_path, sizeof(unix_address->sun_path) };
            const Span<const char> source{ path, length };
            static_cast<void>(target.copyFrom(source));
            address.length = static_cast<int>(sizeof(sockaddr_un));
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
            if (resolved != 0 || found == nullptr) { return AddressResult::failure(SocketCode::UNREACHABLE); }
            Address address{};
            const Span<char> target{ reinterpret_cast<char*>(&address.storage), sizeof(address.storage) };
            const Span<const char> source{ reinterpret_cast<const char*>(found->ai_addr), found->ai_addrlen };
            const bool copied = target.copyFrom(source);
            address.length = static_cast<int>(found->ai_addrlen);
            ::freeaddrinfo(found);
            if (!copied) { return AddressResult::failure(SocketCode::INVALID_ARGUMENT); }
            sockaddr_in* inet_address = reinterpret_cast<sockaddr_in*>(&address.storage);
            inet_address->sin_port = ::htons(endpoint.port);
            return AddressResult::success(address);
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
