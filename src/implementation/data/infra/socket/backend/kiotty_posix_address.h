#pragma once

#if !defined(_WIN32)

#include "core/kiotty_result.h"
#include "data/infra/socket/kiotty_socket_handle.h"
#include "domain/entity/kiotty_socket_code.h"
#include "kiotty_net_base.h"

#include <sys/socket.h>

#include <cstdint>

namespace kiotty::net::implementation
{
    struct Address
    {
        sockaddr_storage storage;
        socklen_t        length;
    };

    enum class AddressRole : uint8_t
    {
        ACTIVE,
        PASSIVE
    };

    using AddressResult = Result<Address, SocketCode>;

    int                     descriptorOf(SocketHandle handle);
    int                     familyOf(KiottyNetTransport transport);
    int                     sendFlags();
    SocketCode              prepareDescriptor(int descriptor);
    AddressResult           resolveAddress(KiottyNetTransport transport, const KiottyNetEndpoint& endpoint, AddressRole role);
    KiottyNetConnectionInfo infoOf(const sockaddr_storage& storage);
    SocketCode              waitFor(int descriptor, short events, uint32_t timeout_milliseconds);
}

#endif
