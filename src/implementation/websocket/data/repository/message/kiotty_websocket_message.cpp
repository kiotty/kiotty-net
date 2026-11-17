#include "websocket/data/repository/message/kiotty_websocket_message.h"

#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/control/kiotty_websocket_timing.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/frame/kiotty_websocket_mask_rules.h"
#include "websocket/data/repository/message/kiotty_websocket_message_rules.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"
#include "websocket/domain/entity/kiotty_websocket_close.h"

namespace kiotty::net::implementation
{
    namespace
    {
        void callMessage(HttpSession& session, const KiottyNetWebSocketMessage& message);
    }

    KiottyNetTransition finishData(HttpSession& session)
    {
        SocketState& socket = socketOf(session);
        const MessageStep step = messageStepOf(socket.frame, socket.message);
        socket.message = step.message;
        if (!step.is_done) { return nextFrame(session); }
        const KiottyNetHttpRequest& request = session.request.request();
        const KiottyNetConnectionHandle connection = request.connection;
        const bool is_broken = isBrokenMessage(step.delivered, spanOf(request.body));
        if (is_broken) { return failSocket(session, CLOSE_INVALID_PAYLOAD); }
        const KiottyNetWebSocketMessage message = appMessageOf(step.delivered, request.body);
        if (!isClosing(session)) { callMessage(session, message); }
        session.request.begin(connection);
        return nextFrame(session);
    }

    bool storePiece(HttpSession& session, Span<const char> piece)
    {
        const SocketFrame& frame = socketOf(session).frame;
        if (isControl(frame.opcode))
        {
            storeControl(session, piece);
            return true;
        }
        const AppendedBodyResult appended = session.request.appendBody(piece);
        if (!appended) { return false; }
        applyMask(appended.value, frame.key, frame.offset);
        return true;
    }

    namespace
    {
        void callMessage(HttpSession& session, const KiottyNetWebSocketMessage& message)
        {
            const WebSocketHandler& handler = handlerOf(session);
            if (handler.role == SocketRole::CLIENT)
            {
                handler.client.events.on_message(session.site.app_context, message);
                return;
            }
            const SocketState& socket = socketOf(session);
            const KiottyNetWebSocketMessageAction on_message = socket.target.on_message;
            on_message(socket.context, socketHandleOf(session), message);
        }
    }
}
