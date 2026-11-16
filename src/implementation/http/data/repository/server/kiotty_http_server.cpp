#include "http/data/repository/server/kiotty_http_server.h"

#include "http/data/repository/machine/kiotty_http_machine.h"
#include "statemachine/data/repository/kiotty_stateful_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        bool areLimitsUsable(const KiottyNetHttpLimits& limits);
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

    KiottyNetCode HttpServer::run(KiottyNetHttpServerHandle self,
                                  const KiottyNetAddress& address,
                                  const KiottyNetTlsServerConfiguration* tls,
                                  KiottyNetAppContext app_context)
    {
        _runs.fetch_add(1);
        _site.is_closing.store(false);
        _site.app_context = app_context;
        _site.owner = self;
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

    KiottyNetCode HttpServer::prepare(const HttpServerConfiguration& configuration)
    {
        const KiottyNetHttpLimits& limits = configuration.limits;
        if (!areLimitsUsable(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _site.limits = limits;
        _site.receive_capacity = configuration.server.receive_capacity;
        _site.send_capacity = configuration.server.send_capacity;
        _site.push_capacity = configuration.server.push_capacity;
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
            const bool has_response = limits.response_bytes >= MIN_HTTP_RESPONSE_BYTES;
            return has_routes && has_headers && has_response;
        }
    }
}
