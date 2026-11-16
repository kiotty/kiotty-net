#include "websocket/data/repository/kiotty_websocket_handler.h"

#include "http/domain/entity/kiotty_http_exchange.h"
#include "websocket/domain/entity/kiotty_websocket_rows.h"
#include "websocket/domain/entity/kiotty_websocket_upgrade.h"

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetTransition onFrameRead(HttpSession& session,
                                        KiottyNetConnectionHandle connection,
                                        KiottyNetConnectionEvent event);
    }

    WebSocketHandler::WebSocketHandler()
        : HttpUpgradeHandler()
        , owner(nullptr)
        , lane(nullptr)
        , client{}
        , role(SocketRole::SERVER)
        , mask_state(0)
        , _targets()
        , _sockets()
        , _links()
        , _count(0)
    {
    }

    void WebSocketHandler::initialize(size_t sockets, size_t routes, SocketRole socket_role)
    {
        if (_sockets.size() < sockets) { _sockets = std::vector<SocketState>(sockets); }
        if (_links.size() < sockets) { _links = std::vector<SocketLink>(sockets); }
        for (SocketState& socket : _sockets)
        {
            socket.reset();
        }
        _count = sockets;
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
        return _sockets[static_cast<size_t>(index)];
    }

    SocketLink& WebSocketHandler::linkAt(int32_t index)
    {
        return _links[static_cast<size_t>(index)];
    }

    Span<SocketLink> WebSocketHandler::links()
    {
        return Span<SocketLink>{ _links.data(), _count };
    }

    KiottyNetTransition WebSocketHandler::answer(HttpSession& session, int32_t upgrade_route)
    {
        const SocketTargetResult found = target(upgrade_route);
        if (!found) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
        return answerUpgrade(session, found.value());
    }

    KiottyNetTransition WebSocketHandler::step(HttpSession& session,
                                               KiottyNetConnectionHandle connection,
                                               KiottyNetConnectionEvent event)
    {
        const KiottyNetConnectionEventType type = event.type;
        if (type == KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN)
        {
            return onSocketWritten(&session, connection, event);
        }
        if (type == KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT)
        {
            return onSocketTimeout(&session, connection, event);
        }
        return onFrameRead(session, connection, event);
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

    namespace
    {
        KiottyNetTransition onFrameRead(HttpSession& session,
                                        KiottyNetConnectionHandle connection,
                                        KiottyNetConnectionEvent event)
        {
            const HttpState state = static_cast<HttpState>(socketOf(session).read.state);
            HttpSession* const context = &session;
            if (state == HttpState::FRAME_LENGTH)
            {
                return onFrameLength(context, connection, event);
            }
            if (state == HttpState::FRAME_MASK)
            {
                return onFrameMask(context, connection, event);
            }
            if (state == HttpState::FRAME_PAYLOAD)
            {
                return onFramePayload(context, connection, event);
            }
            return onFrameStart(context, connection, event);
        }
    }
}
