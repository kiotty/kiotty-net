#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_route_path.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using RoutePathResult = Result<RoutePath, KiottyNetCode>;

    RoutePathResult parseRoutePath(const char* text);
    RoutePathResult parseDirectoryPrefix(const char* text);
    bool            matchesPath(const RoutePath& route, Span<const char> path);
    bool            hasParentSegment(Span<const char> path);
    bool            isSafeRelativePath(Span<const char> path);
}
