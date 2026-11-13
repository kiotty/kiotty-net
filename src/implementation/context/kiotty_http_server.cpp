#include "context/kiotty_http_server.h"

#include "context/kiotty_handles.h"
#include "domain/usecases/http/kiotty_http_machine.h"
#include "domain/usecases/statemachine/kiotty_stateful_handler.h"
#include "domain/usecases/websocket/kiotty_websocket_access.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MIN_RESPONSE_BYTES = RESPONSE_PREFIX_BYTES + 2;

        bool areLimitsUsable(const KiottyNetHttpLimits& limits);
        bool hasEverySocketMessage(HttpRegistry& registry);
    }

    HttpServer::HttpServer()
        : Server()
        , _site()
        , _runs(0)
    {
    }

    HttpServer::~HttpServer() = default;

    KiottyNetCode HttpServer::initialize(PoolHandle id,
                                         const HttpServerConfiguration& configuration)
    {
        const KiottyNetCode base = Server::initialize(id, configuration.server);
        if (base != KIOTTY_NET_CODE_SUCCESS) { return base; }
        return prepare(configuration);
    }

    void HttpServer::reset() noexcept
    {
        Server::reset();
        _site.notices.clear();
    }

    KiottyNetCode HttpServer::run(const KiottyNetAddress& address,
                                  const KiottyNetTlsServerConfiguration* tls,
                                  KiottyNetAppContext app_context)
    {
        if (!hasEverySocketMessage(_site.registry)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _runs.fetch_add(1);
        _site.is_closing.store(false);
        _site.app_context = app_context;
        const KiottyNetStatefulAppContext stateful = httpStatefulOf(_site);
        const StatefulHandler handler(stateful, *this);
        const KiottyNetCode code = Server::run(handler, address, tls);
        _runs.fetch_sub(1);
        return code;
    }

    RegistryResult HttpServer::registry()
    {
        if (_runs.load() > 0) { return RegistryResult::failure(KIOTTY_NET_CODE_BUSY); }
        return RegistryResult::success(_site.registry);
    }

    NoticesResult HttpServer::notices()
    {
        if (_runs.load() > 0) { return NoticesResult::failure(KIOTTY_NET_CODE_BUSY); }
        return NoticesResult::success(_site.notices);
    }

    HttpSite& HttpServer::site()
    {
        return _site;
    }

    void HttpServer::close()
    {
        _site.is_closing.store(true);
        Server::close();
    }

    KiottyNetCode HttpServer::push(int64_t connection,
                                   FrameOpcode opcode,
                                   Span<const char> payload)
    {
        const FrameHead head = buildFrameHead(opcode, payload.length);
        const KiottyNetSpan pieces[] = {
            KiottyNetSpan{ head.bytes, head.length },
            KiottyNetSpan{ payload.data, payload.length },
        };
        return Server::notify(connection, Span<const KiottyNetSpan>{ pieces, 2 });
    }

    KiottyNetWebSocketInfo HttpServer::socketInfo(int64_t id)
    {
        return WebSocketAccess(_site).info(id);
    }

    KiottyNetSpan HttpServer::upgradeHeader(int64_t id, Span<const char> name)
    {
        return WebSocketAccess(_site).header(id, name);
    }

    KiottyNetCode HttpServer::selectProtocol(int64_t id, Span<const char> protocol)
    {
        return WebSocketAccess(_site).select(id, protocol);
    }

    KiottyNetCode HttpServer::sendSocket(int64_t id,
                                         KiottyNetWebSocketOpcode opcode,
                                         KiottyNetSpan data)
    {
        return WebSocketAccess(_site).send(id, opcode, data);
    }

    KiottyNetCode HttpServer::closeSocket(int64_t id, uint16_t status, Span<const char> reason)
    {
        return WebSocketAccess(_site).close(id, status, reason);
    }

    KiottyNetCode HttpServer::prepare(const HttpServerConfiguration& configuration)
    {
        const KiottyNetHttpLimits& limits = configuration.limits;
        if (!areLimitsUsable(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _site.limits = limits;
        _site.receive_capacity = configuration.server.receive_capacity;
        _site.send_capacity = configuration.server.send_capacity;
        _site.owner = static_cast<KiottyNetHttpServer*>(this);
        _site.socket_owner = this;
        _site.lane = this;
        _site.registry.initialize(limits.max_routes);
        _site.notices.clear();
        return _site.sessions.initialize(_site, configuration.server.max_clients, limits);
    }

    namespace
    {
        bool areLimitsUsable(const KiottyNetHttpLimits& limits)
        {
            if (!isInitialized(limits)) { return false; }
            const bool has_routes = limits.max_routes > 0;
            const bool has_headers = limits.max_headers > 0 && limits.max_header_bytes > 0;
            const bool has_response = limits.response_bytes >= MIN_RESPONSE_BYTES;
            return has_routes && has_headers && has_response;
        }

        bool hasEverySocketMessage(HttpRegistry& registry)
        {
            for (int32_t index = 0;; ++index)
            {
                const SocketTargetResult target = registry.find(index);
                if (!target) { return true; }
                if (target.value().on_message == nullptr) { return false; }
            }
        }
    }
}
