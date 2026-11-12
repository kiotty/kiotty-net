#include "data/repository/http/kiotty_http_registry.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint32_t GET_BIT = KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_GET);

        bool isMethodKnown(KiottyNetHttpMethod method);
        bool isWholeServer(Span<const char> path);
        bool isSamePath(const RoutePath& left, const RoutePath& right);
    }

    HttpRegistry::HttpRegistry()
        : _entries()
        , _not_found{}
        , _max_routes(0)
    {
    }

    void HttpRegistry::initialize(size_t max_routes)
    {
        _entries.clear();
        _entries.reserve(max_routes);
        _not_found = KiottyNetHttpRoute{};
        _max_routes = max_routes;
    }

    KiottyNetCode HttpRegistry::addRoute(KiottyNetHttpMethod method,
                                         const char* path,
                                         KiottyNetHttpRoute route)
    {
        if (!isMethodKnown(method)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (route.onRequest == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const RoutePathResult parsed = parseRoutePath(path);
        if (!parsed) { return parsed.code; }
        const uint32_t bit = KIOTTY_NET_HTTP_METHOD_BIT(method);
        if (hasEntry(parsed.value, bit)) { return KIOTTY_NET_CODE_DUPLICATE_RULE; }
        if (_entries.size() >= _max_routes) { return KIOTTY_NET_CODE_TABLE_FULL; }
        RouteEntry entry{};
        (void)entry.path.assign(parsed.value.text);
        entry.route = route;
        entry.methods = bit;
        entry.kind = parsed.value.kind;
        _entries.push_back(entry);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpRegistry::setNotFound(KiottyNetHttpRoute route)
    {
        if (route.onRequest == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _not_found = route;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    RouteMatch HttpRegistry::find(KiottyNetHttpMethod method, Span<const char> path) const
    {
        if (isWholeServer(path)) { return RouteMatch{ {}, allMethods(), MatchKind::OPTIONS }; }
        const RouteMatch exact = findMethod(KIOTTY_NET_HTTP_METHOD_BIT(method), path);
        if (exact.kind == MatchKind::ROUTE) { return exact; }
        const bool is_head = method == KIOTTY_NET_HTTP_METHOD_HEAD;
        const RouteMatch get = is_head ? findMethod(GET_BIT, path) : exact;
        if (get.kind == MatchKind::ROUTE) { return get; }
        return missing(method, exact.allowed);
    }

    bool HttpRegistry::hasEntry(const RoutePath& path, uint32_t method) const
    {
        for (const RouteEntry& entry : _entries)
        {
            const RoutePath stored{ entry.path.span(), entry.kind };
            const bool is_same = isSamePath(stored, path) && (entry.methods & method) != 0;
            if (is_same) { return true; }
        }
        return false;
    }

    RouteMatch HttpRegistry::findMethod(uint32_t method, Span<const char> path) const
    {
        uint32_t allowed = 0;
        for (const RouteEntry& entry : _entries)
        {
            const RoutePath stored{ entry.path.span(), entry.kind };
            if (!matchesPath(stored, path)) { continue; }
            allowed |= entry.methods;
            const RouteMatch found{ entry.route, allowed, MatchKind::ROUTE };
            if ((entry.methods & method) != 0) { return found; }
        }
        return RouteMatch{ {}, allowed, MatchKind::NOT_ALLOWED };
    }

    RouteMatch HttpRegistry::missing(KiottyNetHttpMethod method, uint32_t allowed) const
    {
        const bool has_path = allowed != 0;
        const bool has_not_found = _not_found.onRequest != nullptr;
        if (!has_path && has_not_found) { return RouteMatch{ _not_found, 0, MatchKind::ROUTE }; }
        if (!has_path) { return RouteMatch{ {}, 0, MatchKind::NOT_FOUND }; }
        const bool is_options = method == KIOTTY_NET_HTTP_METHOD_OPTIONS;
        if (is_options) { return RouteMatch{ {}, allowed, MatchKind::OPTIONS }; }
        return RouteMatch{ {}, allowed, MatchKind::NOT_ALLOWED };
    }

    uint32_t HttpRegistry::allMethods() const
    {
        uint32_t methods = 0;
        for (const RouteEntry& entry : _entries)
        {
            methods |= entry.methods;
        }
        return methods;
    }

    namespace
    {
        bool isMethodKnown(KiottyNetHttpMethod method)
        {
            return method >= KIOTTY_NET_HTTP_METHOD_GET && method < KIOTTY_NET_HTTP_METHOD_COUNT;
        }

        bool isWholeServer(Span<const char> path)
        {
            return path.length == 1 && path[0] == '*';
        }

        bool isSamePath(const RoutePath& left, const RoutePath& right)
        {
            if (left.kind != right.kind) { return false; }
            if (left.text.length != right.text.length) { return false; }
            return std::memcmp(left.text.data, right.text.data, left.text.length) == 0;
        }
    }
}
