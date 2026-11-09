#pragma once

#include "core/kiotty_result.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "kiotty_net_base.h"

#include <atomic>

namespace kiotty::net::implementation
{
    using ConnectedResult = Result<ActiveSocket, KiottyNetCode>;

    ConnectedResult connectWithRetry(const KiottyNetClientConfiguration& configuration,
        const std::atomic<bool>& stop);
}
