#include "websocket/data/repository/server/kiotty_websocket_handler.h"

#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
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

    KiottyNetTransition WebSocketHandler::answer(HttpSession& session, int32_t upgrade_route)
    {
        const SocketTargetResult found = target(upgrade_route);
        if (!found) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
        return answerUpgrade(session, found.value());
    }

    KiottyNetTransition WebSocketHandler::step(HttpSession& session,
                                               KiottyNetConnectionHandle,
                                               KiottyNetConnectionEvent event)
    {
        return stepFrame(session, event);
    }

    void WebSocketHandler::end(HttpSession& session, KiottyNetCode reason)
    {
        endSocket(session, reason);
    }

    WebSocketHandler& handlerOf(const HttpSite& site)
    {
        return static_cast<WebSocketHandler&>(*site.upgrade);
    }

    WebSocketHandler& handlerOf(const HttpSession& session)
    {
        return handlerOf(session.site);
    }

    SocketState& socketOf(const HttpSession& session)
    {
        return handlerOf(session).socketAt(session.id.index);
    }

    SocketLink& linkOf(const HttpSession& session)
    {
        return handlerOf(session).linkAt(session.id.index);
    }

}
