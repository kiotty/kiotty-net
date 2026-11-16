#pragma once

#include "base/data/infra/event/kiotty_event_listener.h"
#include "base/data/infra/socket/kiotty_active_socket.h"
#include "base/data/infra/tls/kiotty_tls_engine.h"
#include "base/data/repository/connection/kiotty_connection_parts.h"
#include "base/data/repository/connection/kiotty_connection_state.h"
#include "base/data/repository/connection/kiotty_work_context.h"
#include "base/data/repository/connection/kiotty_work_reporter.h"
#include "base/data/repository/stream/kiotty_connection_stream.h"
#include "base/domain/entity/kiotty_io_action.h"
#include "base/domain/entity/kiotty_phase.h"
#include "core/kiotty_stage.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    class WorkFlow
    {
    public:
        explicit WorkFlow(const ConnectionParts& parts);
        WorkFlow(const WorkFlow&) = delete;
        WorkFlow& operator=(const WorkFlow&) = delete;

        KiottyNetCode    verify(int32_t generation) const;
        ConnectionStream stream();
        KiottyNetCode    closeStream(int32_t generation, const KiottyNetConnectionAction& then);
        IoAction         takeOutcome();
        bool             isReady() const;
        bool             isFinished() const;
        uint8_t          pending() const;

    private:
        StageBase&                _receive;
        StageBase&                _send;
        TlsEngine&                _tls;
        ActiveSocket&             _socket;
        WorkContext&              _work;
        WorkReporter              _reporter;
        const IoEvent&            _read_event;
        const IoEvent&            _write_event;
        const std::atomic<Phase>& _phase;
    };
}
