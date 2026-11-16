#pragma once

#include "core/kiotty_result.h"
#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using RuleResult = Result<const KiottyNetRule&, KiottyNetCode>;

    RuleResult    findRule(const KiottyNetStateMachine& machine,
                           int32_t state,
                           KiottyNetConnectionEventType event);
    bool          isAllowed(const KiottyNetRule& rule, int32_t next);
    KiottyNetCode checkRules(const KiottyNetStateMachine* machine);
}
