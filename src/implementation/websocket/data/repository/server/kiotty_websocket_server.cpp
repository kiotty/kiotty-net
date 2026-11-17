#include "websocket/data/repository/server/kiotty_websocket_server.h"

#include "websocket/data/repository/frame/kiotty_websocket_frame_builder.h"
#include "websocket/data/repository/server/kiotty_websocket_access.h"

namespace kiotty::net::implementation
{
    namespace
    {
        WebSocketAccess serverAccessOf(HttpSessionHost& host, WebSocketHandler& handler)
        {
            return WebSocketAccess(host.registry, handler, host.push_capacity);
        }
    }

    WebSocketServer::WebSocketServer()
        : HttpServer()
        , WebSocketHandler()
    {
    }

    WebSocketServer::~WebSocketServer() = default;

    KiottyNetCode WebSocketServer::initialize(PoolHandle id,
                                              const HttpServerConfiguration& configuration)
    {
        const KiottyNetCode http = HttpServer::initialize(id, configuration);
        if (http != KIOTTY_NET_CODE_SUCCESS) { return http; }
        const size_t sockets = configuration.server.max_clients;
        const size_t routes = configuration.limits.max_routes;
        WebSocketHandler::initialize(sockets, routes, SocketRole::SERVER);
        owner = this;
        lane = this;
        host().upgrade = this;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode WebSocketServer::run(KiottyNetHttpServerHandle self,
                                       const KiottyNetAddress& address,
                                       const KiottyNetTlsServerConfiguration* tls,
                                       KiottyNetAppContext app_context)
    {
        if (!hasEveryMessage()) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return HttpServer::run(self, address, tls, app_context);
    }

    KiottyNetCode WebSocketServer::push(int64_t connection,
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

    KiottyNetWebSocketInfo WebSocketServer::socketInfo(int64_t id)
    {
        return serverAccessOf(host(), *this).info(id);
    }

    KiottyNetSpan WebSocketServer::upgradeHeader(int64_t id, Span<const char> name)
    {
        return serverAccessOf(host(), *this).header(id, name);
    }

    KiottyNetCode WebSocketServer::selectProtocol(int64_t id, Span<const char> protocol)
    {
        return serverAccessOf(host(), *this).select(id, protocol);
    }

    KiottyNetCode WebSocketServer::sendSocket(int64_t id,
                                              KiottyNetWebSocketOpcode opcode,
                                              KiottyNetSpan data)
    {
        return serverAccessOf(host(), *this).send(id, opcode, data);
    }

    KiottyNetCode WebSocketServer::closeSocket(int64_t id,
                                               uint16_t status,
                                               Span<const char> reason)
    {
        return serverAccessOf(host(), *this).close(id, status, reason);
    }
}
