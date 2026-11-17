#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/control/kiotty_websocket_timing.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_parser.h"
#include "websocket/data/repository/message/kiotty_websocket_message.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Session = KiottyNetConnectionContext;
        using Handle = KiottyNetConnectionHandle;
        using Event = KiottyNetConnectionEvent;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status FRAME_TOO_BIG = KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG;

        KiottyNetTransition afterFrameLength(HttpSession& session, uint64_t length);
        KiottyNetTransition beginPayload(HttpSession& session);
        KiottyNetTransition readPayload(HttpSession& session);
    }

    KiottyNetTransition onFrameStart(Session context, Handle, Event event)
    {
        HttpSession& session = sessionOf(context);
        const FrameStartResult parsed = parseFrameStart(spanOf(event.data));
        if (!parsed) { return failSocket(session, parsed.code); }
        const FrameStart& start = parsed.value;
        const bool is_masked = start.masking == FrameMasking::MASKED;
        const bool is_server = handlerOf(session).role == SocketRole::SERVER;
        const bool is_valid = is_masked == is_server && isInOrder(session, start.opcode);
        if (!is_valid) { return failSocket(session, CLOSE_PROTOCOL); }
        socketOf(session).frame = SocketFrame{
            0, 0, MaskKey{}, start.opcode, start.finality, start.masking,
        };
        const size_t extended = extendedLengthBytesOf(start.length_code);
        if (extended > 0) { return readNext(session, HttpState::FRAME_LENGTH, extended); }
        return afterFrameLength(session, start.length_code);
    }

    KiottyNetTransition onFrameLength(Session context, Handle, Event event)
    {
        HttpSession& session = sessionOf(context);
        const FrameLengthResult length = parseExtendedLength(spanOf(event.data));
        if (!length) { return failSocket(session, length.code); }
        return afterFrameLength(session, length.value);
    }

    KiottyNetTransition onFrameMask(Session context, Handle, Event event)
    {
        HttpSession& session = sessionOf(context);
        socketOf(session).frame.key = parseMaskKey(spanOf(event.data));
        return beginPayload(session);
    }

    KiottyNetTransition onFramePayload(Session context, Handle, Event event)
    {
        HttpSession& session = sessionOf(context);
        SocketFrame& frame = socketOf(session).frame;
        const Span<const char> piece = spanOf(event.data);
        if (!storePiece(session, piece)) { return failSocket(session, FRAME_TOO_BIG); }
        frame.offset += piece.length;
        frame.remaining -= piece.length;
        if (frame.remaining > 0) { return readPayload(session); }
        if (isControl(frame.opcode)) { return finishControl(session); }
        return finishData(session);
    }

    KiottyNetTransition readNext(HttpSession& session, HttpState state, size_t length)
    {
        socketOf(session).read = SocketRead{ length, static_cast<int32_t>(state) };
        const uint32_t timeout = readTimeoutOf(session);
        return Next::moveToNextState(state, thenRead(length, timeout));
    }

    KiottyNetTransition nextFrame(HttpSession& session)
    {
        return readNext(session, HttpState::FRAME_START, FRAME_START_BYTES);
    }

    namespace
    {
        KiottyNetTransition afterFrameLength(HttpSession& session, uint64_t length)
        {
            SocketFrame& frame = socketOf(session).frame;
            frame.remaining = length;
            const uint64_t used = session.request.request().body.length;
            const uint64_t room = session.site.limits.max_body_bytes - used;
            const bool is_too_big = !isControl(frame.opcode) && length > room;
            if (is_too_big) { return failSocket(session, FRAME_TOO_BIG); }
            const bool is_masked = frame.masking == FrameMasking::MASKED;
            if (is_masked) { return readNext(session, HttpState::FRAME_MASK, MASK_KEY_BYTES); }
            return beginPayload(session);
        }

        KiottyNetTransition beginPayload(HttpSession& session)
        {
            SocketState& socket = socketOf(session);
            if (isControl(socket.frame.opcode)) { socket.control_length = 0; }
            if (socket.frame.remaining > 0) { return readPayload(session); }
            if (isControl(socket.frame.opcode)) { return finishControl(session); }
            return finishData(session);
        }

        KiottyNetTransition readPayload(HttpSession& session)
        {
            const uint64_t remaining = socketOf(session).frame.remaining;
            const uint64_t capacity = session.site.receive_capacity;
            const uint64_t step = remaining < capacity ? remaining : capacity;
            return readNext(session, HttpState::FRAME_PAYLOAD, static_cast<size_t>(step));
        }
    }
}
