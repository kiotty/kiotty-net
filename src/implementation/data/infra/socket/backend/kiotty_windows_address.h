#pragma once

#if defined(_WIN32)

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "core/kiotty_result.h"
#include "data/infra/socket/kiotty_socket_handle.h"
#include "domain/entity/kiotty_socket_code.h"
#include "kiotty_net_base.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct Address
    {
        sockaddr_storage storage;
        int              length;
    };

    enum class AddressRole : uint8_t
    {
        ACTIVE,
        PASSIVE
    };

    enum class WaitDirection : uint8_t
    {
        READ,
        WRITE
    };

    using AddressResult = Result<Address, SocketCode>;

    SOCKET                  socketOf(SocketHandle handle);
    int                     familyOf(KiottyNetTransport transport);
    int                     clampLength(size_t length);
    SOCKET                  createSocket(KiottyNetTransport transport);
    SocketCode              prepareSocket(SOCKET socket);
    AddressResult           resolveAddress(KiottyNetTransport transport, const KiottyNetEndpoint& endpoint, AddressRole role);
    KiottyNetConnectionInfo infoOf(const sockaddr_storage& storage);
    SocketCode              waitReady(SOCKET socket, WaitDirection direction, uint32_t timeout_milliseconds);
}

#endif
