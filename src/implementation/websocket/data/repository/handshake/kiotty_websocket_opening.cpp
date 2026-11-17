#include "websocket/data/repository/handshake/kiotty_websocket_opening.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Session = KiottyNetConnectionContext;
        using Handle = KiottyNetConnectionHandle;
        using Event = KiottyNetConnectionEvent;

        void callOpen(HttpSession& session);
    }

    KiottyNetTransition onSocketWritten(Session context, Handle, Event event)
    {
        HttpSession& session = sessionOf(context);
        session.sent += event.length;
        const PieceResult piece = pieceAt(session, session.sent);
        if (!piece) { return closeWith(piece.code); }
        if (piece.value.empty()) { return openSocket(session); }
        const KiottyNetSpan data{ piece.value.data, piece.value.length };
        return Next::moveToNextState(HttpState::SOCKET_OPENING, thenWrite(data, 0));
    }

    KiottyNetTransition openSocket(HttpSession& session)
    {
        const KiottyNetConnectionHandle connection = session.request.request().connection;
        session.request.begin(connection);
        session.phase = SessionPhase::SOCKET;
        SocketState& socket = socketOf(session);
        SocketLink& link = linkOf(session);
        socket.is_open = true;
        link.connection.store(connection.id);
        link.path.store(socket.target.path);
        link.closing.store(0);
        link.socket.store(session.id.value());
        callOpen(session);
        Parker* opened = handlerOf(session).client.opened;
        const auto wake = []() {};
        if (opened != nullptr) { opened->unpark(wake); }
        return nextFrame(session);
    }

    namespace
    {
        void callOpen(HttpSession& session)
        {
            const KiottyNetAppContext app_context = session.site.app_context;
            SocketState& socket = socketOf(session);
            socket.context = nullptr;
            if (handlerOf(session).role == SocketRole::CLIENT)
            {
                ClientUpgrade& upgrade = handlerOf(session).client;
                upgrade.has_opened = true;
                const KiottyNetWebSocketClientOpenAction on_open = upgrade.events.on_open;
                if (on_open != nullptr) { on_open(app_context); }
                return;
            }
            const KiottyNetWebSocketOpenAction on_open = socket.target.on_open;
            if (on_open == nullptr) { return; }
            socket.context = on_open(app_context, socketHandleOf(session));
        }
    }
}
