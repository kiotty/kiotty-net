#include "websocket/data/repository/server/kiotty_websocket_handler.h"

#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/handshake/kiotty_websocket_opening.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"

namespace kiotty::net::implementation
{
    WebSocketHandler::WebSocketHandler()
        : HttpUpgradeHandler()
        , owner(nullptr)
        , lane(nullptr)
        , client{}
        , role(SocketRole::SERVER)
        , mask_state(0)
        , _targets()
        , _table()
    {
    }

    void WebSocketHandler::initialize(size_t sockets, size_t routes, SocketRole socket_role)
    {
        _table.initialize(sockets);
        _targets.clear();
        _targets.reserve(routes);
        role = socket_role;
    }

    void WebSocketHandler::addTarget(int32_t path)
    {
        _targets.push_back(WebSocketTarget{ nullptr, nullptr, nullptr, nullptr, path });
    }

    SocketTargetResult WebSocketHandler::target(int32_t path)
    {
        const bool is_inside = path >= 0 && static_cast<size_t>(path) < _targets.size();
        if (!is_inside) { return SocketTargetResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return SocketTargetResult::success(_targets[static_cast<size_t>(path)]);
    }

    bool WebSocketHandler::hasEveryMessage() const
    {
        for (const WebSocketTarget& registered : _targets)
        {
            if (registered.on_message == nullptr) { return false; }
        }
        return true;
    }

    SocketState& WebSocketHandler::socketAt(int32_t index)
    {
        return _table.socketAt(index);
    }

    SocketLink& WebSocketHandler::linkAt(int32_t index)
    {
        return _table.linkAt(index);
    }

    Span<SocketLink> WebSocketHandler::links()
    {
        return _table.links();
    }

    UpgradeAnswer WebSocketHandler::answer(const UpgradeSession& upgrade, int32_t upgrade_route)
    {
        const SocketTargetResult found = target(upgrade_route);
        if (!found) { return UpgradeAnswer::DROP; }
        return answerUpgrade(*this, upgrade, found.value());
    }

    KiottyNetTransition WebSocketHandler::open(const UpgradeSession& upgrade)
    {
        return openSocket(*this, upgrade);
    }

    KiottyNetTransition WebSocketHandler::step(const UpgradeSession& upgrade,
                                               KiottyNetConnectionEvent event)
    {
        return stepFrame(*this, upgrade, event);
    }

    void WebSocketHandler::end(const UpgradeSession& upgrade, KiottyNetCode reason)
    {
        endSocket(*this, upgrade, reason);
    }

    SocketState& socketOf(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        return handler.socketAt(upgrade.id.index);
    }

    SocketLink& linkOf(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        return handler.linkAt(upgrade.id.index);
    }

}
