#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "domain/usecases/http/kiotty_http_route_path.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    enum class MatchKind : uint8_t
    {
        ROUTE,
        NOT_FOUND,
        NOT_ALLOWED,
        OPTIONS
    };

    struct RouteMatch
    {
        KiottyNetHttpRoute route;
        uint32_t           allowed;
        MatchKind          kind;
    };

    class HttpRegistry
    {
    public:
        HttpRegistry();

        void          initialize(size_t max_routes);
        KiottyNetCode addRoute(KiottyNetHttpMethod method,
                               const char* path,
                               KiottyNetHttpRoute route);
        KiottyNetCode setNotFound(KiottyNetHttpRoute route);
        RouteMatch    find(KiottyNetHttpMethod method, Span<const char> path) const;

    private:
        struct RouteEntry
        {
            FixedString<KIOTTY_NET_MAX_URL_LENGTH> path;
            KiottyNetHttpRoute                     route;
            uint32_t                               methods;
            PathKind                               kind;
        };

        bool       hasEntry(const RoutePath& path, uint32_t method) const;
        RouteMatch findMethod(uint32_t method, Span<const char> path) const;
        RouteMatch missing(KiottyNetHttpMethod method, uint32_t allowed) const;
        uint32_t   allMethods() const;

        std::vector<RouteEntry> _entries;
        KiottyNetHttpRoute      _not_found;
        size_t                  _max_routes;
    };
}
