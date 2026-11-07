#pragma once

#include "data/infra/socket/kiotty_active_socket.h"
#include "data/infra/tls/kiotty_tls_profile.h"
#include "domain/entity/kiotty_connection_limits.h"
#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    struct ConnectionConfiguration
    {
        ActiveSocket               socket;
        ConnectionLimits           limits;
        KiottyNetConnectionContext context;
        const char*                hostname;
        TlsProfile&                profile;
    };

    static_assert(sizeof(ConnectionConfiguration) == sizeof(ActiveSocket) + sizeof(ConnectionLimits) + 3 * sizeof(void*), "ConnectionConfiguration: unexpected padding");
}
