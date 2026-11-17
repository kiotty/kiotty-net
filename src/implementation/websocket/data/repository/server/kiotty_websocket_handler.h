#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "websocket/data/datasource/kiotty_websocket_socket_table.h"
#include "websocket/data/repository/endpoint/kiotty_websocket_endpoint.h"
#include "websocket/data/repository/send/kiotty_websocket_lane.h"
#include "websocket/data/state/kiotty_websocket_client_state.h"
#include "websocket/data/state/kiotty_websocket_socket_state.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    using SocketTargetResult = Result<WebSocketTarget&, KiottyNetCode>;

    struct SocketSide
    {
        KiottyNetWebSocketEndpoint* owner;
        PushLane*                   lane;
        ClientUpgrade*              client;
        SocketRole                  role;
    };

    class WebSocketHandler : public HttpUpgradeHandler
    {
    public:
        WebSocketHandler();

        void               initialize(size_t sockets, size_t routes, const SocketSide& side);
        void               addTarget(int32_t path);
        SocketTargetResult target(int32_t path);
        bool               hasEveryMessage() const;
        SocketState&       socketAt(int32_t index);
        Span<SocketLink>   links();
        const SocketSide&  side() const;

        UpgradeAnswer       answer(const UpgradeSession& upgrade, int32_t upgrade_route) override;
        KiottyNetTransition open(const UpgradeSession& upgrade) override;
        KiottyNetTransition step(const UpgradeSession& upgrade,
                                 KiottyNetConnectionEvent event) override;
        void                end(const UpgradeSession& upgrade, KiottyNetCode reason) override;

    protected:
        ~WebSocketHandler() = default;

    private:
        SocketSide                   _side;
        std::vector<WebSocketTarget> _targets;
        SocketTable                  _table;
    };

    SocketState&      socketOf(WebSocketHandler& handler, const UpgradeSession& upgrade);
    SocketLink&       linkOf(WebSocketHandler& handler, const UpgradeSession& upgrade);
}
