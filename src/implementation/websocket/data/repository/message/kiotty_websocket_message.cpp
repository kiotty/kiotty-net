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
        void callMessage(WebSocketHandler& handler,
                         const UpgradeSession& upgrade,
                         const KiottyNetWebSocketMessage& message);
    }

    KiottyNetTransition finishData(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        SocketState& socket = socketOf(handler, upgrade);
        const MessageStep step = messageStepOf(socket.frame, socket.message);
        socket.message = step.message;
        if (!step.is_done) { return nextFrame(handler, upgrade); }
        const Span<const char> body{ upgrade.message.data, socket.message_length };
        const bool is_broken = isBrokenMessage(step.delivered, body);
        if (is_broken) { return failSocket(handler, upgrade, CLOSE_INVALID_PAYLOAD); }
        const KiottyNetSpan data{ body.data, body.length };
        const KiottyNetWebSocketMessage message = appMessageOf(step.delivered, data);
        if (!isClosing(handler, upgrade)) { callMessage(handler, upgrade, message); }
        socket.message_length = 0;
        return nextFrame(handler, upgrade);
    }

    bool storePiece(WebSocketHandler& handler,
                    const UpgradeSession& upgrade,
                    Span<const char> piece)
    {
        SocketState& socket = socketOf(handler, upgrade);
        const SocketFrame& frame = socket.frame;
        if (isControl(frame.opcode))
        {
            storeControl(handler, upgrade, piece);
            return true;
        }
        const size_t used = socket.message_length;
        if (piece.length > upgrade.message.length - used) { return false; }
        const Span<char> target{ upgrade.message.data + used, piece.length };
        (void)target.copyFrom(piece);
        socket.message_length = used + piece.length;
        applyMask(target, frame.key, frame.offset);
        return true;
    }

    namespace
    {
        void callMessage(WebSocketHandler& handler,
                         const UpgradeSession& upgrade,
                         const KiottyNetWebSocketMessage& message)
        {
            if (handler.role == SocketRole::CLIENT)
            {
                handler.client.events.on_message(upgrade.app_context, message);
                return;
            }
            const SocketState& socket = socketOf(handler, upgrade);
            const KiottyNetWebSocketMessageAction on_message = socket.target.on_message;
            on_message(socket.context, socketHandleOf(handler, upgrade), message);
        }
    }
}
