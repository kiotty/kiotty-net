#pragma once

#include "core/kiotty_span.h"
#include "core/kiotty_stage.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/repository/connection/kiotty_connection_parts.h"
#include "data/repository/connection/kiotty_work_context.h"
#include "domain/entity/kiotty_io_action.h"
#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_io_payload.h"
#include "domain/entity/kiotty_phase.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace kiotty::net::implementation
{
    class SendFlow
    {
    public:
        explicit SendFlow(const ConnectionParts& parts);
        SendFlow(const SendFlow&) = delete;
        SendFlow& operator=(const SendFlow&) = delete;

        IoAction      pick(const std::lock_guard<std::mutex>& notification);
        IoPayload     payload(const IoAction& write);
        void          release(const IoCompletion& sent, std::mutex& notification);
        size_t        notificationWritable();
        KiottyNetCode stageNotification(Span<const KiottyNetSpan> pieces);
        void          limit(uint32_t timeout_milliseconds);
        void          clearDeadline();
        uint64_t      deadline() const;

    private:
        IoAction select();
        IoAction encoded(IoAction write);
        bool     isBlocked() const;
        void     startDeadline();

        StageBase&                _send;
        StageBase&                _notify;
        TlsEngine&                _tls;
        const IoEvent&            _write_event;
        const WorkContext&        _work;
        const std::atomic<Phase>& _phase;
        std::atomic<uint64_t>     _deadline;
        uint32_t                  _timeout;
    };
}
