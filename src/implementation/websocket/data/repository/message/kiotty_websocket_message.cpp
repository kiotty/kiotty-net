#include "websocket/data/repository/message/kiotty_websocket_message.h"

#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/control/kiotty_websocket_timing.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/frame/kiotty_websocket_mask_rules.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"
#include "websocket/domain/entity/kiotty_websocket_close.h"

namespace kiotty::net::implementation
{
    namespace
    {
        void deliver(HttpSession& session, FrameOpcode opcode, KiottyNetSpan data);
        void callMessage(HttpSession& session, const KiottyNetWebSocketMessage& message);
    }

    KiottyNetTransition finishData(HttpSession& session)
    {
        SocketState& socket = socketOf(session);
        const SocketFrame& frame = socket.frame;
        const bool is_first = frame.opcode != FrameOpcode::CONTINUATION;
        if (is_first) { socket.message = frame.opcode; }
        if (frame.finality == FrameFinality::MORE) { return nextFrame(session); }
        const FrameOpcode message = socket.message;
        socket.message = FrameOpcode::CONTINUATION;
        const KiottyNetHttpRequest& request = session.request.request();
        const KiottyNetConnectionHandle connection = request.connection;
        const bool is_text = message == FrameOpcode::TEXT;
        const bool is_broken = is_text && !isUtf8(spanOf(request.body));
        if (is_broken) { return failSocket(session, CLOSE_INVALID_PAYLOAD); }
        if (!isClosing(session)) { deliver(session, message, request.body); }
        session.request.begin(connection);
        return nextFrame(session);
    }

    bool isInOrder(const HttpSession& session, FrameOpcode opcode)
    {
        if (isControl(opcode)) { return true; }
        const bool is_in_message = socketOf(session).message != FrameOpcode::CONTINUATION;
        const bool is_continuation = opcode == FrameOpcode::CONTINUATION;
        return is_in_message == is_continuation;
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
        void deliver(HttpSession& session, FrameOpcode opcode, KiottyNetSpan data)
        {
            const bool is_text = opcode == FrameOpcode::TEXT;
            const KiottyNetWebSocketOpcode kind =
                is_text ? KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT : KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY;
            const KiottyNetWebSocketMessage message{ kind, data };
            callMessage(session, message);
        }

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
