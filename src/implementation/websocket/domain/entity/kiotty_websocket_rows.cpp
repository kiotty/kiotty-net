#include "websocket/domain/entity/kiotty_websocket_rows.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_bytes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "websocket/data/repository/kiotty_websocket_handler.h"
#include "websocket/domain/entity/kiotty_websocket_close.h"
#include "websocket/domain/entity/kiotty_websocket_lane.h"
#include "websocket/domain/entity/kiotty_websocket_timing.h"
#include "websocket/domain/entity/kiotty_websocket_upgrade.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Session = KiottyNetConnectionContext;
        using Handle = KiottyNetConnectionHandle;
        using Event = KiottyNetConnectionEvent;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status   TOO_BIG = KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG;

        KiottyNetTransition     readNext(HttpSession& session, HttpState state, size_t length);
        KiottyNetTransition     nextFrame(HttpSession& session);
        KiottyNetTransition     afterFrameLength(HttpSession& session, uint64_t length);
        KiottyNetTransition     beginPayload(HttpSession& session);
        KiottyNetTransition     readPayload(HttpSession& session);
        KiottyNetTransition     finishControl(HttpSession& session);
        KiottyNetTransition     finishData(HttpSession& session);
        KiottyNetTransition     answerClose(HttpSession& session);
        KiottyNetTransition     failSocket(HttpSession& session, Status failure);
        KiottyNetTransition     writeClose(HttpSession& session,
                                           Span<const char> payload,
                                           KiottyNetCode code);
        KiottyNetCode           closeCodeOf(Status status);
        MaskKey                 closeMaskOf(const HttpSession& session);
        void                    callOpen(HttpSession& session);
        void                    callMessage(HttpSession& session,
                                            const KiottyNetWebSocketMessage& message);
        void                    callClose(HttpSession& session,
                                          const KiottyNetWebSocketClose& close);
        bool                    isInOrder(const HttpSession& session, FrameOpcode opcode);
        bool                    storePiece(HttpSession& session, Span<const char> piece);
        void                    storeControl(HttpSession& session, Span<const char> piece);
        void                    deliver(HttpSession& session,
                                        FrameOpcode opcode,
                                        KiottyNetSpan data);
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
        if (!storePiece(session, piece)) { return failSocket(session, TOO_BIG); }
        frame.offset += piece.length;
        frame.remaining -= piece.length;
        if (frame.remaining > 0) { return readPayload(session); }
        if (isControl(frame.opcode)) { return finishControl(session); }
        return finishData(session);
    }

    KiottyNetTransition onSocketTimeout(Session context, Handle, Event)
    {
        HttpSession& session = sessionOf(context);
        if (isClosing(session)) { return closeWith(KIOTTY_NET_CODE_TIMEOUT); }
        const Span<const char> empty{ nullptr, 0 };
        PushLane& lane = *handlerOf(session).lane;
        SocketState& socket = socketOf(session);
        const bool is_pong_late = socket.ping_deadline != 0;
        if (is_pong_late) { (void)pushClose(lane, linkOf(session), GOING_AWAY, empty); }
        else
        {
            (void)pushFrame(lane, linkOf(session), FrameOpcode::PING, empty);
            const uint32_t pong_timeout = session.site.limits.websocket_pong_timeout_milliseconds;
            socket.ping_deadline = deadlineAfter(pong_timeout);
        }
        const SocketRead read = socket.read;
        return readNext(session, static_cast<HttpState>(read.state), read.length);
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

    void endSocket(HttpSession& session, KiottyNetCode reason)
    {
        SocketState& socket = socketOf(session);
        SocketLink& link = linkOf(session);
        if (!socket.is_open) { return; }
        socket.is_open = false;
        link.socket.store(0);
        const KiottyNetWebSocketClose close = closeOf(session, reason);
        callClose(session, close);
        link.path.store(NO_SOCKET_PATH);
    }

    namespace
    {
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

        KiottyNetTransition afterFrameLength(HttpSession& session, uint64_t length)
        {
            SocketFrame& frame = socketOf(session).frame;
            frame.remaining = length;
            const uint64_t used = session.request.request().body.length;
            const uint64_t room = session.site.limits.max_body_bytes - used;
            const bool is_too_big = !isControl(frame.opcode) && length > room;
            if (is_too_big) { return failSocket(session, TOO_BIG); }
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

        KiottyNetTransition finishControl(HttpSession& session)
        {
            SocketState& socket = socketOf(session);
            const Span<const char> payload{ socket.control, socket.control_length };
            const FrameOpcode opcode = socket.frame.opcode;
            if (opcode == FrameOpcode::CLOSE) { return answerClose(session); }
            if (opcode == FrameOpcode::PONG) { socket.ping_deadline = 0; }
            const bool is_ping = opcode == FrameOpcode::PING;
            if (is_ping && !isClosing(session))
            {
                PushLane& lane = *handlerOf(session).lane;
                (void)pushFrame(lane, linkOf(session), FrameOpcode::PONG, payload);
            }
            return nextFrame(session);
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

        KiottyNetTransition answerClose(HttpSession& session)
        {
            SocketState& socket = socketOf(session);
            const Span<const char> payload{ socket.control, socket.control_length };
            const ClosePayloadResult parsed = parseClosePayload(payload);
            if (!parsed) { return failSocket(session, parsed.code); }
            socket.has_peer_close = true;
            socket.peer_status = parsed.value.status;
            uint16_t open = 0;
            const uint16_t echoed = parsed.value.status;
            const bool is_first = linkOf(session).closing.compare_exchange_strong(open, echoed);
            if (!is_first) { return closeWith(KIOTTY_NET_CODE_SUCCESS); }
            const size_t status_bytes = echoed == NO_STATUS ? 0 : STATUS_BYTES;
            const Span<const char> echo{ payload.data, status_bytes };
            return writeClose(session, echo, KIOTTY_NET_CODE_SUCCESS);
        }

        KiottyNetTransition failSocket(HttpSession& session, Status failure)
        {
            const uint16_t status = static_cast<uint16_t>(failure);
            const KiottyNetCode code = closeCodeOf(failure);
            uint16_t open = 0;
            const bool is_first = linkOf(session).closing.compare_exchange_strong(open, status);
            if (!is_first) { return closeWith(code); }
            const char bytes[STATUS_BYTES] = {
                static_cast<char>(static_cast<uint8_t>(status >> BITS_PER_BYTE)),
                static_cast<char>(static_cast<uint8_t>(status & BYTE_MASK)),
            };
            return writeClose(session, Span<const char>{ bytes, STATUS_BYTES }, code);
        }

        KiottyNetTransition writeClose(HttpSession& session,
                                       Span<const char> payload,
                                       KiottyNetCode code)
        {
            const MaskKey key = closeMaskOf(session);
            const bool is_client = handlerOf(session).role == SocketRole::CLIENT;
            const FrameOpcode close = FrameOpcode::CLOSE;
            const FrameHead head = is_client ? buildMaskedFrameHead(close, payload.length, key)
                                             : buildFrameHead(close, payload.length);
            char* const outgoing_bytes = socketOf(session).outgoing;
            const Span<char> outgoing{ outgoing_bytes, CLOSE_FRAME_BYTES };
            (void)outgoing.copyFrom(head.span());
            const Span<char> body{ outgoing_bytes + head.length, payload.length };
            (void)body.copyFrom(payload);
            applyMask(body, key, 0);
            const KiottyNetSpan data{ outgoing_bytes, head.length + payload.length };
            const KiottyNetConnectionAction farewell = thenCloseWith(data, code);
            return Next::moveToNextState(HttpState::CLOSING, farewell);
        }

        MaskKey closeMaskOf(const HttpSession& session)
        {
            WebSocketHandler& handler = handlerOf(session);
            if (handler.role == SocketRole::SERVER) { return MaskKey{}; }
            return nextMaskKey(handler.mask_state);
        }

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

        void callClose(HttpSession& session, const KiottyNetWebSocketClose& close)
        {
            const KiottyNetAppContext app_context = session.site.app_context;
            const WebSocketHandler& handler = handlerOf(session);
            if (handler.role == SocketRole::CLIENT)
            {
                const ClientEvents& events = handler.client.events;
                const KiottyNetWebSocketClientCloseAction on_close = events.on_close;
                if (on_close != nullptr) { on_close(app_context, close); }
                return;
            }
            const SocketState& socket = socketOf(session);
            const KiottyNetWebSocketCloseAction on_close = socket.target.on_close;
            if (on_close == nullptr) { return; }
            on_close(app_context, socket.context, close);
        }

        KiottyNetCode closeCodeOf(Status status)
        {
            if (status == TOO_BIG) { return KIOTTY_NET_CODE_REQUEST_TOO_LARGE; }
            return KIOTTY_NET_CODE_PROTOCOL_ERROR;
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

        void storeControl(HttpSession& session, Span<const char> piece)
        {
            SocketState& socket = socketOf(session);
            const size_t offset = socket.frame.offset;
            const Span<char> target{ socket.control + offset, piece.length };
            (void)target.copyFrom(piece);
            applyMask(target, socket.frame.key, offset);
            socket.control_length = offset + piece.length;
        }

        void deliver(HttpSession& session, FrameOpcode opcode, KiottyNetSpan data)
        {
            const bool is_text = opcode == FrameOpcode::TEXT;
            const KiottyNetWebSocketOpcode kind =
                is_text ? KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT : KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY;
            const KiottyNetWebSocketMessage message{ kind, data };
            callMessage(session, message);
        }
    }
}
