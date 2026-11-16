#pragma once

#include "core/kiotty_span.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class SendKind : uint8_t
    {
        WRITE,
        FAREWELL,
        DONE
    };

    struct SendStep
    {
        Span<const char> chunk;
        SendKind         kind;
    };

    enum class AfterResponse : uint8_t
    {
        NEXT_EXCHANGE,
        CLOSE
    };
}
