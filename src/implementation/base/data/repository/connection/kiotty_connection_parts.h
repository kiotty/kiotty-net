#pragma once

#include "base/data/infra/event/kiotty_event_listener.h"
#include "base/data/infra/socket/kiotty_active_socket.h"
#include "base/data/infra/tls/kiotty_tls_engine.h"
#include "base/data/repository/connection/kiotty_work_context.h"
#include "base/domain/entity/kiotty_phase.h"
#include "core/kiotty_stage.h"

#include <atomic>

namespace kiotty::net::implementation
{
    struct ConnectionParts
    {
        std::atomic<Phase>& phase;
        WorkContext&        work;
        IoEvent&            read_event;
        IoEvent&            write_event;
        StageBase&          receive;
        StageBase&          send;
        StageBase&          notify;
        TlsEngine&          tls;
        ActiveSocket&       socket;
    };

    static_assert(sizeof(ConnectionParts) == 9 * sizeof(void*),
                  "ConnectionParts: unexpected padding");
}
