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

    static constexpr size_t CONNECTION_CONFIGURATION_BYTES =
        sizeof(ActiveSocket) + sizeof(ConnectionLimits) + 3 * sizeof(void*);
    static_assert(sizeof(ConnectionConfiguration) == CONNECTION_CONFIGURATION_BYTES,
                  "ConnectionConfiguration: unexpected padding");
}
