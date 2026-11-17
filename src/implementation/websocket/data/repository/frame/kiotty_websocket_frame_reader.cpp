#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/machine/kiotty_http_close.h"
#include "http/domain/entity/kiotty_http_state.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/control/kiotty_websocket_timing.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_rules.h"
#include "websocket/data/repository/message/kiotty_websocket_message.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status FRAME_TOO_BIG = KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG;

        KiottyNetTransition readFrameBytes(WebSocketHandler& handler, const UpgradeSession& upgrade,
                                           WebSocketState state,
                                           Span<const char> bytes);
        KiottyNetTransition readFrameStart(WebSocketHandler& handler,
                                           const UpgradeSession& upgrade,
                                           Span<const char> bytes);
        KiottyNetTransition readFrameLength(WebSocketHandler& handler,
                                            const UpgradeSession& upgrade,
                                            Span<const char> bytes);
        KiottyNetTransition readFrameMask(WebSocketHandler& handler,
                                          const UpgradeSession& upgrade,
                                          Span<const char> bytes);
        KiottyNetTransition readFramePiece(WebSocketHandler& handler,
                                           const UpgradeSession& upgrade,
                                           Span<const char> bytes);
        KiottyNetTransition afterFrameLength(WebSocketHandler& handler,
                                             const UpgradeSession& upgrade,
                                             uint64_t length);
        KiottyNetTransition beginPayload(WebSocketHandler& handler, const UpgradeSession& upgrade);
        KiottyNetTransition continueFrame(WebSocketHandler& handler, const UpgradeSession& upgrade);
    }

    KiottyNetTransition stepFrame(WebSocketHandler& handler,
                                  const UpgradeSession& upgrade,
                                  KiottyNetConnectionEvent event)
    {
        const WebSocketState state = socketOf(handler, upgrade).read.state;
        if (state == WebSocketState::OPENING) { return closeWith(KIOTTY_NET_CODE_UNHANDLED_EVENT); }
        const KiottyNetConnectionEventType type = event.type;
        const bool is_timeout = type == KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT;
        if (is_timeout) { return timeoutSocket(handler, upgrade); }
        const bool is_read = type == KIOTTY_NET_CONNECTION_EVENT_TYPE_READ;
        if (!is_read) { return closeWith(KIOTTY_NET_CODE_UNHANDLED_EVENT); }
        const Span<const char> bytes = spanOf(event.data);
        return readFrameBytes(handler, upgrade, state, bytes);
    }

    KiottyNetTransition readNext(WebSocketHandler& handler,
                                 const UpgradeSession& upgrade,
                                 FrameRead read)
    {
        socketOf(handler, upgrade).read = read;
        const uint32_t timeout = readTimeoutOf(handler, upgrade);
        return Next::moveToNextState(HttpState::UPGRADED, thenRead(read.length, timeout));
    }

    KiottyNetTransition nextFrame(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        return readNext(handler,
                        upgrade,
                        FrameRead{ FRAME_START_BYTES, WebSocketState::FRAME_START });
    }

    namespace
    {
        KiottyNetTransition readFrameBytes(WebSocketHandler& handler, const UpgradeSession& upgrade,
                                           WebSocketState state,
                                           Span<const char> bytes)
        {
            switch (state)
            {
            case WebSocketState::FRAME_LENGTH:
                return readFrameLength(handler, upgrade, bytes);
            case WebSocketState::FRAME_MASK:
                return readFrameMask(handler, upgrade, bytes);
            case WebSocketState::FRAME_PAYLOAD:
                return readFramePiece(handler, upgrade, bytes);
            case WebSocketState::OPENING:
            case WebSocketState::FRAME_START:
                break;
            }
            return readFrameStart(handler, upgrade, bytes);
        }

        KiottyNetTransition readFrameStart(WebSocketHandler& handler,
                                           const UpgradeSession& upgrade,
                                           Span<const char> bytes)
        {
            const FrameStartResult parsed = parseFrameStart(bytes);
            if (!parsed) { return failSocket(handler, upgrade, parsed.code); }
            const FrameStart& start = parsed.value;
            SocketState& socket = socketOf(handler, upgrade);
            const SocketRole role = handler.side().role;
            const bool is_valid = isAcceptedStart(start, role, socket.message);
            if (!is_valid) { return failSocket(handler, upgrade, CLOSE_PROTOCOL); }
            socket.frame = frameOf(start);
            const size_t extended = extendedLengthBytesOf(start.length_code);
            const FrameRead length_read{ extended, WebSocketState::FRAME_LENGTH };
            if (extended > 0) { return readNext(handler, upgrade, length_read); }
            return afterFrameLength(handler, upgrade, start.length_code);
        }

        KiottyNetTransition readFrameLength(WebSocketHandler& handler,
                                            const UpgradeSession& upgrade,
                                            Span<const char> bytes)
        {
            const FrameLengthResult length = parseExtendedLength(bytes);
            if (!length) { return failSocket(handler, upgrade, length.code); }
            return afterFrameLength(handler, upgrade, length.value);
        }

        KiottyNetTransition readFrameMask(WebSocketHandler& handler,
                                          const UpgradeSession& upgrade,
                                          Span<const char> bytes)
        {
            socketOf(handler, upgrade).frame.key = parseMaskKey(bytes);
            return beginPayload(handler, upgrade);
        }

        KiottyNetTransition readFramePiece(WebSocketHandler& handler,
                                           const UpgradeSession& upgrade,
                                           Span<const char> bytes)
        {
            const bool is_stored = storePiece(handler, upgrade, bytes);
            if (!is_stored) { return failSocket(handler, upgrade, FRAME_TOO_BIG); }
            SocketState& socket = socketOf(handler, upgrade);
            socket.frame = advanceFrame(socket.frame, bytes.length);
            return continueFrame(handler, upgrade);
        }

        KiottyNetTransition afterFrameLength(WebSocketHandler& handler,
                                             const UpgradeSession& upgrade,
                                             uint64_t length)
        {
            SocketState& socket = socketOf(handler, upgrade);
            SocketFrame& frame = socket.frame;
            frame.remaining = length;
            const uint64_t room = upgrade.message.length - socket.message_length;
            if (isOversized(frame, room)) { return failSocket(handler, upgrade, FRAME_TOO_BIG); }
            const bool is_masked = frame.masking == FrameMasking::MASKED;
            const FrameRead mask_read{ MASK_KEY_BYTES, WebSocketState::FRAME_MASK };
            if (is_masked) { return readNext(handler, upgrade, mask_read); }
            return beginPayload(handler, upgrade);
        }

        KiottyNetTransition beginPayload(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            SocketState& socket = socketOf(handler, upgrade);
            if (isControl(socket.frame.opcode)) { socket.control_length = 0; }
            return continueFrame(handler, upgrade);
        }

        KiottyNetTransition continueFrame(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            const SocketFrame& frame = socketOf(handler, upgrade).frame;
            const FrameEnd end = frameEndOf(frame);
            const size_t capacity = upgrade.receive_capacity;
            if (end == FrameEnd::CONTROL) { return finishControl(handler, upgrade); }
            if (end == FrameEnd::DATA) { return finishData(handler, upgrade); }
            const FrameRead payload_read = payloadReadOf(frame, capacity);
            return readNext(handler, upgrade, payload_read);
        }
    }
}
