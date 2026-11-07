#pragma once

#include "core/kiotty_stage.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/repository/connection/kiotty_work_context.h"
#include "domain/entity/kiotty_phase.h"

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

    static_assert(sizeof(ConnectionParts) == 9 * sizeof(void*), "ConnectionParts: unexpected padding");
}
