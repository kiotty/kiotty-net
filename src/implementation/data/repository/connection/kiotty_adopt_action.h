#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_stage.h"
#include "domain/entity/kiotty_io_action.h"
#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    using AdoptResult = Result<IoAction, KiottyNetCode>;

    AdoptResult adoptAction(const KiottyNetConnectionAction& returned, StageBase& send);
}
