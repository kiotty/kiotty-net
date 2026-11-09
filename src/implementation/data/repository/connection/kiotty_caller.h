#pragma once

#include "core/kiotty_stage.h"
#include "core/pool/kiotty_pool_handle.h"
#include "domain/entity/kiotty_handler.h"
#include "domain/entity/kiotty_io_action.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct Caller
    {
        const Handler&                 handler;
        const KiottyNetConnectionInfo& info;
        StageBase&                     send;
        int32_t&                       machine_state;
        KiottyNetConnectionContext     context;
        PoolHandle                     id;

        KiottyNetConnectionHandle handle() const;
        KiottyNetConnectionStream stream() const;
        void                      reject(KiottyNetCode code, const char* message) const;
        IoAction                  admit(const KiottyNetConnectionAction& returned) const;
    };

    static constexpr size_t CALLER_BYTES =
        4 * sizeof(void*) + sizeof(KiottyNetConnectionContext) + sizeof(PoolHandle);
    static_assert(sizeof(Caller) == CALLER_BYTES, "Caller: unexpected padding");
}
