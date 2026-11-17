#include "websocket/data/repository/control/kiotty_websocket_control.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_bytes.h"
#include "http/data/repository/machine/kiotty_http_close.h"
#include "http/domain/entity/kiotty_http_state.h"
#include "websocket/data/repository/control/kiotty_websocket_close_rules.h"
#include "websocket/data/repository/control/kiotty_websocket_control_rules.h"
#include "websocket/data/repository/control/kiotty_websocket_timing.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_builder.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/frame/kiotty_websocket_mask_rules.h"
#include "websocket/data/repository/send/kiotty_websocket_lane.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status TOO_BIG = KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG;

        KiottyNetTransition answerClose(WebSocketHandler& handler, const UpgradeSession& upgrade);
        KiottyNetTransition writeClose(WebSocketHandler& handler, const UpgradeSession& upgrade,
                                       Span<const char> payload,
                                       KiottyNetCode code);
        KiottyNetCode       closeCodeOf(Status status);
        MaskKey             closeMaskOf(WebSocketHandler& handler);
        void                callClose(WebSocketHandler& handler,
                                      const UpgradeSession& upgrade,
                                      const KiottyNetWebSocketClose& close);
    }

    KiottyNetTransition timeoutSocket(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        SocketState& socket = socketOf(handler, upgrade);
        const bool is_closing = isClosing(handler, upgrade);
        const TimeoutAction action = timeoutActionOf(is_closing, socket.ping_deadline);
        if (action == TimeoutAction::CLOSE) { return closeWith(KIOTTY_NET_CODE_TIMEOUT); }
        const Span<const char> empty{ nullptr, 0 };
        PushLane& lane = *handler.side().lane;
        const bool is_pong_late = action == TimeoutAction::SEND_CLOSE;
        if (is_pong_late) { (void)pushClose(lane, linkOf(handler, upgrade), GOING_AWAY, empty); }
        else
        {
            (void)pushFrame(lane, linkOf(handler, upgrade), FrameOpcode::PING, empty);
            const uint32_t pong_timeout = upgrade.limits.websocket_pong_timeout_milliseconds;
            socket.ping_deadline = deadlineAfter(pong_timeout);
        }
        return readNext(handler, upgrade, socket.read);
    }

    void endSocket(WebSocketHandler& handler, const UpgradeSession& upgrade, KiottyNetCode reason)
    {
        SocketState& socket = socketOf(handler, upgrade);
        SocketLink& link = linkOf(handler, upgrade);
        if (!socket.is_open) { return; }
        socket.is_open = false;
        link.socket.store(0);
        const KiottyNetWebSocketClose close = closeOf(handler, upgrade, reason);
        callClose(handler, upgrade, close);
        link.path.store(NO_SOCKET_PATH);
    }

    KiottyNetTransition finishControl(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        SocketState& socket = socketOf(handler, upgrade);
        const Span<const char> payload{ socket.control, socket.control_length };
        const bool is_closing = isClosing(handler, upgrade);
        const ControlAction action = controlActionOf(socket.frame.opcode, is_closing);
        if (action == ControlAction::ANSWER_CLOSE) { return answerClose(handler, upgrade); }
        if (action == ControlAction::CLEAR_PING) { socket.ping_deadline = 0; }
        if (action == ControlAction::SEND_PONG)
        {
            PushLane& lane = *handler.side().lane;
            (void)pushFrame(lane, linkOf(handler, upgrade), FrameOpcode::PONG, payload);
        }
        return nextFrame(handler, upgrade);
    }

    KiottyNetTransition failSocket(WebSocketHandler& handler,
                                   const UpgradeSession& upgrade,
                                   Status failure)
    {
        const uint16_t status = static_cast<uint16_t>(failure);
        const KiottyNetCode code = closeCodeOf(failure);
        uint16_t open = 0;
        const bool is_first = linkOf(handler,
                                     upgrade).closing.compare_exchange_strong(open,
                                     status);
        if (!is_first) { return closeWith(code); }
        const char bytes[STATUS_BYTES] = {
            static_cast<char>(static_cast<uint8_t>(status >> BITS_PER_BYTE)),
            static_cast<char>(static_cast<uint8_t>(status & BYTE_MASK)),
        };
        return writeClose(handler, upgrade, Span<const char>{ bytes, STATUS_BYTES }, code);
    }

    void storeControl(WebSocketHandler& handler,
                      const UpgradeSession& upgrade,
                      Span<const char> piece)
    {
        SocketState& socket = socketOf(handler, upgrade);
        const size_t offset = socket.frame.offset;
        const Span<char> target{ socket.control + offset, piece.length };
        (void)target.copyFrom(piece);
        applyMask(target, socket.frame.key, offset);
        socket.control_length = offset + piece.length;
    }

    namespace
    {
        KiottyNetTransition answerClose(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            SocketState& socket = socketOf(handler, upgrade);
            const Span<const char> payload{ socket.control, socket.control_length };
            const ClosePayloadResult parsed = parseClosePayload(payload);
            if (!parsed) { return failSocket(handler, upgrade, parsed.code); }
            socket.has_peer_close = true;
            socket.peer_status = parsed.value.status;
            uint16_t open = 0;
            const uint16_t echoed = parsed.value.status;
            const bool is_first = linkOf(handler,
                                         upgrade).closing.compare_exchange_strong(open,
                                         echoed);
            if (!is_first) { return closeWith(KIOTTY_NET_CODE_SUCCESS); }
            const size_t status_bytes = echoed == NO_STATUS ? 0 : STATUS_BYTES;
            const Span<const char> echo{ payload.data, status_bytes };
            return writeClose(handler, upgrade, echo, KIOTTY_NET_CODE_SUCCESS);
        }

        KiottyNetTransition writeClose(WebSocketHandler& handler, const UpgradeSession& upgrade,
                                       Span<const char> payload,
                                       KiottyNetCode code)
        {
            const MaskKey key = closeMaskOf(handler);
            const bool is_client = handler.side().role == SocketRole::CLIENT;
            const FrameOpcode close = FrameOpcode::CLOSE;
            const FrameHead head = is_client ? buildMaskedFrameHead(close, payload.length, key)
                                             : buildFrameHead(close, payload.length);
            char* const outgoing_bytes = socketOf(handler, upgrade).outgoing;
            const Span<char> outgoing{ outgoing_bytes, CLOSE_FRAME_BYTES };
            (void)outgoing.copyFrom(headBytesOf(head));
            const Span<char> body{ outgoing_bytes + head.length, payload.length };
            (void)body.copyFrom(payload);
            applyMask(body, key, 0);
            const KiottyNetSpan data{ outgoing_bytes, head.length + payload.length };
            const KiottyNetConnectionAction farewell = thenCloseWith(data, code);
            return Next::moveToNextState(HttpState::CLOSING, farewell);
        }

        KiottyNetCode closeCodeOf(Status status)
        {
            if (status == TOO_BIG) { return KIOTTY_NET_CODE_REQUEST_TOO_LARGE; }
            return KIOTTY_NET_CODE_PROTOCOL_ERROR;
        }

        MaskKey closeMaskOf(WebSocketHandler& handler)
        {
            ClientUpgrade* const client = handler.side().client;
            if (client == nullptr) { return MaskKey{}; }
            return nextMaskKey(client->mask_state);
        }

        void callClose(WebSocketHandler& handler,
                       const UpgradeSession& upgrade,
                       const KiottyNetWebSocketClose& close)
        {
            const KiottyNetAppContext app_context = upgrade.app_context;
            const ClientUpgrade* const client = handler.side().client;
            if (client != nullptr)
            {
                const ClientEvents& events = client->events;
                const KiottyNetWebSocketClientCloseAction on_close = events.on_close;
                if (on_close != nullptr) { on_close(app_context, close); }
                return;
            }
            const SocketState& socket = socketOf(handler, upgrade);
            const KiottyNetWebSocketCloseAction on_close = socket.target.on_close;
            if (on_close == nullptr) { return; }
            on_close(app_context, socket.context, close);
        }
    }
}
