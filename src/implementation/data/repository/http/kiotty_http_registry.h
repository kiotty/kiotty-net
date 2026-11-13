#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "data/repository/websocket/kiotty_websocket_socket.h"
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
        STATIC,
        SOCKET,
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
        WebSocketTarget    socket;
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

    using SocketIndexResult = Result<int32_t, KiottyNetCode>;
    using SocketTargetResult = Result<WebSocketTarget&, KiottyNetCode>;

    class HttpRegistry
    {
    public:
        HttpRegistry();

        void          initialize(size_t max_routes);
        KiottyNetCode addRoute(KiottyNetHttpMethod method,
                               const char* path,
                               KiottyNetHttpRoute route);
        SocketIndexResult  addRoute(const char* path);
        KiottyNetCode setNotFound(KiottyNetHttpRoute route);
        KiottyNetCode addDirectory(const char* url_prefix, const char* directory);
        KiottyNetCode addFile(const char* url_path, const char* file_path);
        KiottyNetCode addMemory(const char* url_path,
                                KiottyNetHttpContentType type,
                                KiottyNetSpan data);
        RouteMatch    find(KiottyNetHttpMethod method, Span<const char> path) const;
        int32_t       find(Span<const char> socket_path) const;
        SocketTargetResult find(int32_t index);

    private:
        using Location = FixedString<KIOTTY_NET_MAX_PATH_LENGTH>;

        struct RouteEntry
        {
            FixedString<KIOTTY_NET_MAX_URL_LENGTH> path;
            Location                               location;
            KiottyNetHttpRoute                     route;
            KiottyNetSpan                          memory;
            KiottyNetHttpContentType               type;
            uint32_t                               methods;
            int32_t                                socket;
            PathKind                               kind;
            StaticKind                             source;
        };

        KiottyNetCode addStatic(const RoutePath& path, const StaticSource& source);
        KiottyNetCode admit(const RoutePath& path, uint32_t methods) const;
        bool          hasEntry(const RoutePath& path, uint32_t method) const;
        RouteMatch    findMethod(uint32_t method, Span<const char> path) const;
        RouteMatch    entryMatch(const RouteEntry& entry,
                                 Span<const char> prefix,
                                 uint32_t allowed) const;
        RouteMatch    missing(KiottyNetHttpMethod method, uint32_t allowed) const;
        uint32_t      allMethods() const;

        std::vector<RouteEntry>      _entries;
        std::vector<WebSocketTarget> _sockets;
        KiottyNetHttpRoute      _not_found;
        size_t                  _max_routes;
    };
}
