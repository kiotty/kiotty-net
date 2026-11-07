#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_stage.h"
#include "data/infra/tls/kiotty_tls_engine.h"
#include "data/repository/connection/kiotty_caller.h"
#include "data/repository/connection/kiotty_connection_parts.h"
#include "domain/entity/kiotty_io_action.h"
#include "domain/entity/kiotty_io_payload.h"
#include "kiotty_net_base.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class ReceiveFlow
    {
    public:
        explicit ReceiveFlow(const ConnectionParts& parts);
        ReceiveFlow(const ReceiveFlow&) = delete;
        ReceiveFlow& operator=(const ReceiveFlow&) = delete;

        IoAction  drive(IoAction action, const Caller& caller);
        IoPayload room(IoPurpose purpose);
        void      commit(IoPurpose purpose, size_t length);
        int32_t   machineState() const;
        void      setMachineState(int32_t state);

    private:
        using RefillResult = Result<size_t, KiottyNetCode>;

        IoAction     deliver(KiottyNetSpan data, const Caller& caller);
        RefillResult refill();
        IoAction     refuse(KiottyNetCode code, const Caller& caller);
        StageBase&   stageFor(IoPurpose purpose);

        StageBase&           _receive;
        TlsEngine&           _tls;
        std::atomic<int32_t> _machine_state;
    };
}
