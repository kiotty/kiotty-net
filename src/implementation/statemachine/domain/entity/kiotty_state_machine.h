#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct StateMachine
    {
        Span<const KiottyNetRule> rules;
        int32_t                   initial;
    };
}
