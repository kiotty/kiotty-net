#pragma once

#include "core/kiotty_span.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class PathKind : uint8_t
    {
        EXACT,
        PREFIX
    };

    struct RoutePath
    {
        Span<const char> text;
        PathKind         kind;
    };
}
