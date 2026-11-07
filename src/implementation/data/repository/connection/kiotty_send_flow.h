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
#include <mutex>

namespace kiotty::net::implementation
{
    class SendFlow
    {
    public:
        explicit SendFlow(const ConnectionParts& parts);
        SendFlow(const SendFlow&) = delete;
        SendFlow& operator=(const SendFlow&) = delete;

        IoAction      pick(std::mutex& notification);
        IoPayload     payload(const IoAction& write);
        void          release(const IoCompletion& sent, std::mutex& notification);
        size_t        notificationWritable();
        KiottyNetCode stageNotification(KiottyNetSpan data);

    private:
        IoAction select(std::mutex& notification);
        IoAction selectNotification(std::mutex& notification);
        IoAction selectStage(StageBase& stage, IoPurpose purpose);
        IoAction encoded(IoAction write, Span<char> plain);
        bool     isBlocked() const;

        StageBase&                _send;
        StageBase&                _notify;
        TlsEngine&                _tls;
        const IoEvent&            _write_event;
        const WorkContext&        _work;
        const std::atomic<Phase>& _phase;
    };
}
