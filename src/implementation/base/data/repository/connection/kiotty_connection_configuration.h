#pragma once

#include "base/data/infra/socket/kiotty_active_socket.h"
#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "base/domain/entity/kiotty_connection_limits.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    struct ConnectionConfiguration
    {
        ActiveSocket               socket;
        ConnectionLimits           limits;
        KiottyNetConnectionContext context;
        Span<const char>           hostname;
        TlsProfile&                profile;
    };

    static constexpr size_t CONNECTION_CONFIGURATION_BYTES =
        sizeof(ActiveSocket) + sizeof(ConnectionLimits) + sizeof(Span<const char>)
        + 2 * sizeof(void*);
    static_assert(sizeof(ConnectionConfiguration) == CONNECTION_CONFIGURATION_BYTES,
                  "ConnectionConfiguration: unexpected padding");
}
