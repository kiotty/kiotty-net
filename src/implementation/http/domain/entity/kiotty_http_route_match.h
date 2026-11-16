#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr int32_t NO_UPGRADE_ROUTE = -1;

    enum class MatchKind : uint8_t
    {
        ROUTE,
        STATIC,
        UPGRADE,
        NOT_FOUND,
        NOT_ALLOWED,
        OPTIONS
    };

    enum class StaticKind : uint8_t
    {
        NONE,
        DIRECTORY,
        FILE,
        MEMORY
    };

    struct StaticTarget
    {
        Span<const char>         prefix;
        Span<const char>         location;
        KiottyNetSpan            memory;
        KiottyNetHttpContentType type;
        StaticKind               kind;
    };

    struct RouteMatch
    {
        KiottyNetHttpRoute route;
        StaticTarget       target;
        int32_t            upgrade;
        uint32_t           allowed;
        MatchKind          kind;
    };

    struct StaticSource
    {
        Span<const char>         location;
        KiottyNetSpan            memory;
        KiottyNetHttpContentType type;
        StaticKind               kind;
    };
}
