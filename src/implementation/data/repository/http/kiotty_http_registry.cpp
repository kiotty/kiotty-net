#include "data/repository/http/kiotty_http_registry.h"

#include "core/kiotty_text.h"
#include "domain/usecases/http/kiotty_http_content_type.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint32_t GET_BIT = KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_GET);
        constexpr uint32_t HEAD_BIT = KIOTTY_NET_HTTP_METHOD_BIT(KIOTTY_NET_HTTP_METHOD_HEAD);
        constexpr uint32_t STATIC_METHODS = GET_BIT | HEAD_BIT;
        constexpr uint32_t ALL_METHODS = 0xFFFFFFFFU;
        constexpr size_t   MAX_LOCATION_LENGTH = KIOTTY_NET_MAX_PATH_LENGTH - 2;
        constexpr KiottyNetSpan NO_MEMORY{ nullptr, 0 };

        using TextLocation = Result<Span<const char>, KiottyNetCode>;

        bool         isMethodKnown(KiottyNetHttpMethod method);
        bool         isWholeServer(Span<const char> path);
        bool         isSamePath(const RoutePath& left, const RoutePath& right);
        bool         isSeparator(char character);
        TextLocation parseLocation(const char* text);
        KiottyNetCode checkMemory(KiottyNetHttpContentType type, KiottyNetSpan data);
        RouteMatch   matchOf(MatchKind kind, uint32_t allowed);
        uint32_t     takenMethodsOf(uint32_t methods, int32_t socket);
    }

    HttpRegistry::HttpRegistry()
        : _entries()
        , _sockets()
        , _not_found{}
        , _max_routes(0)
    {
    }

    void HttpRegistry::initialize(size_t max_routes)
    {
        _entries.clear();
        _entries.reserve(max_routes);
        _sockets.clear();
        _sockets.reserve(max_routes);
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
        const KiottyNetCode admitted = admit(parsed.value, bit);
        if (admitted != KIOTTY_NET_CODE_SUCCESS) { return admitted; }
        RouteEntry entry{};
        (void)entry.path.assign(parsed.value.text);
        entry.route = route;
        entry.methods = bit;
        entry.socket = NO_SOCKET_PATH;
        entry.kind = parsed.value.kind;
        entry.source = StaticKind::NONE;
        _entries.push_back(entry);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpRegistry::addRoute(const char* path,
                                         const KiottyNetWebSocketHandler& handler,
                                         KiottyNetAppContext app_context)
    {
        const bool has_handler = handler.onOpen != nullptr && handler.onMessage != nullptr;
        if (!has_handler) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const RoutePathResult parsed = parseRoutePath(path);
        if (!parsed) { return parsed.code; }
        const KiottyNetCode admitted = admit(parsed.value, ALL_METHODS);
        if (admitted != KIOTTY_NET_CODE_SUCCESS) { return admitted; }
        const int32_t index = static_cast<int32_t>(_sockets.size());
        _sockets.push_back(WebSocketTarget{ handler, app_context, index });
        RouteEntry entry{};
        (void)entry.path.assign(parsed.value.text);
        entry.methods = GET_BIT;
        entry.socket = index;
        entry.kind = parsed.value.kind;
        entry.source = StaticKind::NONE;
        _entries.push_back(entry);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpRegistry::setNotFound(KiottyNetHttpRoute route)
    {
        if (route.onRequest == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _not_found = route;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpRegistry::addDirectory(const char* url_prefix, const char* directory)
    {
        const RoutePathResult parsed = parseDirectoryPrefix(url_prefix);
        if (!parsed) { return parsed.code; }
        const TextLocation location = parseLocation(directory);
        if (!location) { return location.code; }
        const KiottyNetHttpContentType type = KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
        const StaticSource source{ location.value, NO_MEMORY, type, StaticKind::DIRECTORY };
        return addStatic(parsed.value, source);
    }

    KiottyNetCode HttpRegistry::addFile(const char* url_path, const char* file_path)
    {
        const RoutePathResult parsed = parseRoutePath(url_path);
        if (!parsed) { return parsed.code; }
        const TextLocation location = parseLocation(file_path);
        if (!location) { return location.code; }
        const KiottyNetHttpContentType type = contentTypeOfPath(location.value);
        const StaticSource source{ location.value, NO_MEMORY, type, StaticKind::FILE };
        return addStatic(parsed.value, source);
    }

    KiottyNetCode HttpRegistry::addMemory(const char* url_path,
                                          KiottyNetHttpContentType type,
                                          KiottyNetSpan data)
    {
        const RoutePathResult parsed = parseRoutePath(url_path);
        if (!parsed) { return parsed.code; }
        const KiottyNetCode checked = checkMemory(type, data);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const Span<const char> no_location{ nullptr, 0 };
        const StaticSource source{ no_location, data, type, StaticKind::MEMORY };
        return addStatic(parsed.value, source);
    }

    RouteMatch HttpRegistry::find(KiottyNetHttpMethod method, Span<const char> path) const
    {
        if (isWholeServer(path)) { return matchOf(MatchKind::OPTIONS, allMethods()); }
        const bool is_head = method == KIOTTY_NET_HTTP_METHOD_HEAD;
        const uint32_t bit = KIOTTY_NET_HTTP_METHOD_BIT(method);
        const uint32_t wanted = is_head ? bit | GET_BIT : bit;
        const RouteMatch found = findMethod(wanted, path);
        const bool is_missing = found.kind == MatchKind::NOT_ALLOWED;
        if (!is_missing) { return found; }
        return missing(method, found.allowed);
    }

    int32_t HttpRegistry::find(Span<const char> socket_path) const
    {
        for (const RouteEntry& entry : _entries)
        {
            if (entry.socket == NO_SOCKET_PATH) { continue; }
            const Span<const char> text = entry.path.span();
            if (text.length != socket_path.length) { continue; }
            if (std::memcmp(text.data, socket_path.data, text.length) == 0) { return entry.socket; }
        }
        return NO_SOCKET_PATH;
    }

    KiottyNetCode HttpRegistry::addStatic(const RoutePath& path, const StaticSource& source)
    {
        const KiottyNetCode admitted = admit(path, STATIC_METHODS);
        if (admitted != KIOTTY_NET_CODE_SUCCESS) { return admitted; }
        RouteEntry entry{};
        (void)entry.path.assign(path.text);
        (void)entry.location.assign(source.location);
        const bool needs_separator = source.kind == StaticKind::DIRECTORY
                                     && !isSeparator(source.location[source.location.length - 1]);
        if (needs_separator) { (void)entry.location.append(Span<const char>{ "/", 1 }); }
        entry.memory = source.memory;
        entry.type = source.type;
        entry.methods = STATIC_METHODS;
        entry.socket = NO_SOCKET_PATH;
        entry.kind = path.kind;
        entry.source = source.kind;
        _entries.push_back(entry);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpRegistry::admit(const RoutePath& path, uint32_t methods) const
    {
        if (hasEntry(path, methods)) { return KIOTTY_NET_CODE_DUPLICATE_RULE; }
        if (_entries.size() >= _max_routes) { return KIOTTY_NET_CODE_TABLE_FULL; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool HttpRegistry::hasEntry(const RoutePath& path, uint32_t method) const
    {
        for (const RouteEntry& entry : _entries)
        {
            const RoutePath stored{ entry.path.span(), entry.kind };
            const uint32_t taken = takenMethodsOf(entry.methods, entry.socket);
            const bool is_same = isSamePath(stored, path) && (taken & method) != 0;
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
            if ((entry.methods & method) == 0) { continue; }
            return entryMatch(entry, stored.text, allowed);
        }
        return matchOf(MatchKind::NOT_ALLOWED, allowed);
    }

    RouteMatch HttpRegistry::entryMatch(const RouteEntry& entry,
                                        Span<const char> prefix,
                                        uint32_t allowed) const
    {
        const StaticTarget target{
            prefix, entry.location.span(), entry.memory, entry.type, entry.source,
        };
        const WebSocketTarget no_socket{ KiottyNetWebSocketHandler{}, nullptr, NO_SOCKET_PATH };
        RouteMatch match{ entry.route, target, no_socket, allowed, MatchKind::ROUTE };
        if (entry.source != StaticKind::NONE) { match.kind = MatchKind::STATIC; }
        if (entry.socket == NO_SOCKET_PATH) { return match; }
        match.kind = MatchKind::SOCKET;
        match.socket = _sockets[static_cast<size_t>(entry.socket)];
        return match;
    }

    RouteMatch HttpRegistry::missing(KiottyNetHttpMethod method, uint32_t allowed) const
    {
        const bool has_path = allowed != 0;
        const bool has_not_found = _not_found.onRequest != nullptr;
        RouteMatch not_found = matchOf(MatchKind::ROUTE, 0);
        not_found.route = _not_found;
        if (!has_path && has_not_found) { return not_found; }
        if (!has_path) { return matchOf(MatchKind::NOT_FOUND, 0); }
        const bool is_options = method == KIOTTY_NET_HTTP_METHOD_OPTIONS;
        if (is_options) { return matchOf(MatchKind::OPTIONS, allowed); }
        return matchOf(MatchKind::NOT_ALLOWED, allowed);
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

        bool isSeparator(char character)
        {
            return character == '/' || character == '\\';
        }

        TextLocation parseLocation(const char* text)
        {
            const TextResult parsed = parseText(text, MAX_LOCATION_LENGTH);
            if (!parsed) { return TextLocation::failure(parsed.code); }
            const KiottyNetCode invalid = KIOTTY_NET_CODE_INVALID_ARGUMENT;
            if (parsed.value.empty()) { return TextLocation::failure(invalid); }
            return TextLocation::success(parsed.value);
        }

        KiottyNetCode checkMemory(KiottyNetHttpContentType type, KiottyNetSpan data)
        {
            const bool is_listed = type >= KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM
                                   && type < KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION
                                   && type != KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM;
            const bool is_missing = data.data == nullptr && data.length > 0;
            if (!is_listed || is_missing) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            const Span<const char> bytes{ data.data, data.length };
            const bool is_text = textKindOf(type) != TextKind::BINARY;
            if (is_text && !isUtf8(bytes)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        RouteMatch matchOf(MatchKind kind, uint32_t allowed)
        {
            const StaticTarget none{
                Span<const char>{ nullptr, 0 },
                Span<const char>{ nullptr, 0 },
                NO_MEMORY,
                KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM,
                StaticKind::NONE,
            };
            const WebSocketTarget no_socket{ KiottyNetWebSocketHandler{}, nullptr, NO_SOCKET_PATH };
            return RouteMatch{ KiottyNetHttpRoute{}, none, no_socket, allowed, kind };
        }

        uint32_t takenMethodsOf(uint32_t methods, int32_t socket)
        {
            return socket == NO_SOCKET_PATH ? methods : ALL_METHODS;
        }
    }
}
