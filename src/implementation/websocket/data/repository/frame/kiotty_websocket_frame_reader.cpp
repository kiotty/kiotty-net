#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/control/kiotty_websocket_timing.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_rules.h"
#include "websocket/data/repository/handshake/kiotty_websocket_opening.h"
#include "websocket/data/repository/message/kiotty_websocket_message.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status FRAME_TOO_BIG = KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG;

        KiottyNetTransition stepOpening(HttpSession& session, KiottyNetConnectionEvent event);
        KiottyNetTransition readFrameBytes(HttpSession& session,
                                           WebSocketState state,
                                           Span<const char> bytes);
        KiottyNetTransition readFrameStart(HttpSession& session, Span<const char> bytes);
        KiottyNetTransition readFrameLength(HttpSession& session, Span<const char> bytes);
        KiottyNetTransition readFrameMask(HttpSession& session, Span<const char> bytes);
        KiottyNetTransition readFramePiece(HttpSession& session, Span<const char> bytes);
        KiottyNetTransition afterFrameLength(HttpSession& session, uint64_t length);
        KiottyNetTransition beginPayload(HttpSession& session);
        KiottyNetTransition continueFrame(HttpSession& session);
    }

    KiottyNetTransition stepFrame(HttpSession& session, KiottyNetConnectionEvent event)
    {
        const WebSocketState state = socketOf(session).read.state;
        if (state == WebSocketState::OPENING) { return stepOpening(session, event); }
        const KiottyNetConnectionEventType type = event.type;
        if (type == KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT) { return timeoutSocket(session); }
        const bool is_read = type == KIOTTY_NET_CONNECTION_EVENT_TYPE_READ;
        if (!is_read) { return closeWith(KIOTTY_NET_CODE_UNHANDLED_EVENT); }
        const Span<const char> bytes = spanOf(event.data);
        return readFrameBytes(session, state, bytes);
    }

    KiottyNetTransition readNext(HttpSession& session, FrameRead read)
    {
        socketOf(session).read = read;
        const uint32_t timeout = readTimeoutOf(session);
        return Next::moveToNextState(HttpState::UPGRADED, thenRead(read.length, timeout));
    }

    KiottyNetTransition nextFrame(HttpSession& session)
    {
        return readNext(session, FrameRead{ FRAME_START_BYTES, WebSocketState::FRAME_START });
    }

    namespace
    {
        KiottyNetTransition stepOpening(HttpSession& session, KiottyNetConnectionEvent event)
        {
            const KiottyNetConnectionEventType type = event.type;
            if (type == KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN)
            {
                return sendOpening(session, event.length);
            }
            if (type == KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT)
            {
                return closeWith(KIOTTY_NET_CODE_TIMEOUT);
            }
            return closeWith(KIOTTY_NET_CODE_UNHANDLED_EVENT);
        }

        KiottyNetTransition readFrameBytes(HttpSession& session,
                                           WebSocketState state,
                                           Span<const char> bytes)
        {
            switch (state)
            {
            case WebSocketState::FRAME_LENGTH:
                return readFrameLength(session, bytes);
            case WebSocketState::FRAME_MASK:
                return readFrameMask(session, bytes);
            case WebSocketState::FRAME_PAYLOAD:
                return readFramePiece(session, bytes);
            case WebSocketState::OPENING:
            case WebSocketState::FRAME_START:
                break;
            }
            return readFrameStart(session, bytes);
        }

        KiottyNetTransition readFrameStart(HttpSession& session, Span<const char> bytes)
        {
            const FrameStartResult parsed = parseFrameStart(bytes);
            if (!parsed) { return failSocket(session, parsed.code); }
            const FrameStart& start = parsed.value;
            SocketState& socket = socketOf(session);
            const SocketRole role = handlerOf(session).role;
            const bool is_valid = isAcceptedStart(start, role, socket.message);
            if (!is_valid) { return failSocket(session, CLOSE_PROTOCOL); }
            socket.frame = frameOf(start);
            const size_t extended = extendedLengthBytesOf(start.length_code);
            const FrameRead length_read{ extended, WebSocketState::FRAME_LENGTH };
            if (extended > 0) { return readNext(session, length_read); }
            return afterFrameLength(session, start.length_code);
        }

        KiottyNetTransition readFrameLength(HttpSession& session, Span<const char> bytes)
        {
            const FrameLengthResult length = parseExtendedLength(bytes);
            if (!length) { return failSocket(session, length.code); }
            return afterFrameLength(session, length.value);
        }

        KiottyNetTransition readFrameMask(HttpSession& session, Span<const char> bytes)
        {
            socketOf(session).frame.key = parseMaskKey(bytes);
            return beginPayload(session);
        }

        KiottyNetTransition readFramePiece(HttpSession& session, Span<const char> bytes)
        {
            if (!storePiece(session, bytes)) { return failSocket(session, FRAME_TOO_BIG); }
            SocketState& socket = socketOf(session);
            socket.frame = advanceFrame(socket.frame, bytes.length);
            return continueFrame(session);
        }

        KiottyNetTransition afterFrameLength(HttpSession& session, uint64_t length)
        {
            SocketFrame& frame = socketOf(session).frame;
            frame.remaining = length;
            const uint64_t used = session.request.request().body.length;
            const uint64_t room = session.host.limits.max_body_bytes - used;
            if (isOversized(frame, room)) { return failSocket(session, FRAME_TOO_BIG); }
            const bool is_masked = frame.masking == FrameMasking::MASKED;
            const FrameRead mask_read{ MASK_KEY_BYTES, WebSocketState::FRAME_MASK };
            if (is_masked) { return readNext(session, mask_read); }
            return beginPayload(session);
        }

        KiottyNetTransition beginPayload(HttpSession& session)
        {
            SocketState& socket = socketOf(session);
            if (isControl(socket.frame.opcode)) { socket.control_length = 0; }
            return continueFrame(session);
        }

        KiottyNetTransition continueFrame(HttpSession& session)
        {
            const SocketFrame& frame = socketOf(session).frame;
            const FrameEnd end = frameEndOf(frame);
            const size_t capacity = session.host.receive_capacity;
            if (end == FrameEnd::CONTROL) { return finishControl(session); }
            if (end == FrameEnd::DATA) { return finishData(session); }
            const FrameRead payload_read = payloadReadOf(frame, capacity);
            return readNext(session, payload_read);
        }
    }
}
