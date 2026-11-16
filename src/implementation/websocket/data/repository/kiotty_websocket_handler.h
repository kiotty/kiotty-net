#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/repository/kiotty_http_site.h"
#include "http/data/repository/kiotty_http_upgrade_handler.h"
#include "websocket/data/repository/kiotty_websocket_endpoint.h"
#include "websocket/data/repository/kiotty_websocket_socket.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    using SocketTargetResult = Result<WebSocketTarget&, KiottyNetCode>;

    class WebSocketHandler : public HttpUpgradeHandler
    {
    public:
        WebSocketHandler();

        void               initialize(size_t sockets, size_t routes, SocketRole socket_role);
        void               addTarget(int32_t path);
        SocketTargetResult target(int32_t path);
        bool               hasEveryMessage() const;
        SocketState&       socketAt(int32_t index);
        SocketLink&        linkAt(int32_t index);
        Span<SocketLink>   links();

        KiottyNetTransition answer(HttpSession& session, int32_t upgrade_route) override;
        KiottyNetTransition step(HttpSession& session,
                                 KiottyNetConnectionHandle connection,
                                 KiottyNetConnectionEvent event) override;
        void                end(HttpSession& session, KiottyNetCode reason) override;

        KiottyNetWebSocketEndpoint* owner;
        PushLane*                   lane;
        ClientUpgrade               client;
        SocketRole                  role;
        std::atomic<uint64_t>       mask_state;

    protected:
        ~WebSocketHandler() = default;

    private:
        std::vector<WebSocketTarget> _targets;
        std::vector<SocketState>     _sockets;
        std::vector<SocketLink>      _links;
        size_t                       _count;
    };

    WebSocketHandler& handlerOf(const HttpSite& site);
    WebSocketHandler& handlerOf(const HttpSession& session);
    SocketState&      socketOf(const HttpSession& session);
    SocketLink&       linkOf(const HttpSession& session);
}
