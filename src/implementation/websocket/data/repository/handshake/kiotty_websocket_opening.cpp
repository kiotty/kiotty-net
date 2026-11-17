#include "websocket/data/repository/handshake/kiotty_websocket_opening.h"

#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        void callOpen(WebSocketHandler& handler, const UpgradeSession& upgrade);
    }

    KiottyNetTransition openSocket(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        const KiottyNetConnectionHandle connection = upgrade.request.connection;
        SocketState& socket = socketOf(handler, upgrade);
        SocketLink& link = linkOf(handler, upgrade);
        socket.is_open = true;
        socket.message_length = 0;
        socket.connection = upgrade.info;
        link.connection.store(connection.id);
        link.path.store(socket.target.path);
        link.closing.store(0);
        link.socket.store(upgrade.id.value());
        callOpen(handler, upgrade);
        Parker* opened = handler.client.opened;
        const auto wake = []() {};
        if (opened != nullptr) { opened->unpark(wake); }
        return nextFrame(handler, upgrade);
    }

    namespace
    {
        void callOpen(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            const KiottyNetAppContext app_context = upgrade.app_context;
            SocketState& socket = socketOf(handler, upgrade);
            socket.context = nullptr;
            if (handler.role == SocketRole::CLIENT)
            {
                ClientUpgrade& client = handler.client;
                client.has_opened = true;
                const KiottyNetWebSocketClientOpenAction on_open = client.events.on_open;
                if (on_open != nullptr) { on_open(app_context); }
                return;
            }
            const KiottyNetWebSocketOpenAction on_open = socket.target.on_open;
            if (on_open == nullptr) { return; }
            socket.context = on_open(app_context, socketHandleOf(handler, upgrade));
        }
    }
}
