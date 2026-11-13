#include "domain/usecases/websocket/kiotty_websocket_rows.h"

#include "core/kiotty_text.h"
#include "data/repository/http/kiotty_http_file_body.h"
#include "data/repository/http/kiotty_http_site.h"
#include "domain/entity/kiotty_action.h"
#include "domain/usecases/http/kiotty_http_exchange.h"
#include "domain/usecases/websocket/kiotty_websocket_close.h"
#include "domain/usecases/websocket/kiotty_websocket_lane.h"
#include "domain/usecases/websocket/kiotty_websocket_timing.h"
#include "domain/usecases/websocket/kiotty_websocket_upgrade.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Context = KiottyNetConnectionContext;
        using Connection = KiottyNetConnectionHandle;
        using Event = KiottyNetConnectionEvent;
        using Status = KiottyNetWebSocketStatus;

        constexpr Status   PROTOCOL_ERROR = KIOTTY_NET_WEB_SOCKET_STATUS_PROTOCOL_ERROR;
        constexpr Status   INVALID_PAYLOAD = KIOTTY_NET_WEB_SOCKET_STATUS_INVALID_PAYLOAD;
        constexpr Status   TOO_BIG = KIOTTY_NET_WEB_SOCKET_STATUS_MESSAGE_TOO_BIG;
        constexpr Status   GOING_AWAY = KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY;
        constexpr uint16_t NO_STATUS = KIOTTY_NET_WEB_SOCKET_STATUS_NO_STATUS;
        constexpr size_t   STATUS_BYTES = 2;
        constexpr uint32_t BITS_PER_BYTE = 8;
        constexpr uint16_t BYTE_MASK = 0xFF;
        constexpr uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ULL;

        Span<const char>        spanOf(KiottyNetSpan data);
        KiottyNetTransition     readNext(HttpSession& session, HttpState state, size_t length);
        KiottyNetTransition     nextFrame(HttpSession& session);
        KiottyNetTransition     afterLength(HttpSession& session, uint64_t length);
        KiottyNetTransition     beginPayload(HttpSession& session);
        KiottyNetTransition     readPayload(HttpSession& session);
        KiottyNetTransition     finishControl(HttpSession& session);
        KiottyNetTransition     finishData(HttpSession& session);
        KiottyNetTransition     answerClose(HttpSession& session);
        KiottyNetTransition     failSocket(HttpSession& session, Status failure);
        KiottyNetTransition     writeClose(HttpSession& session,
                                           Span<const char> payload,
                                           KiottyNetCode code);
        KiottyNetCode           codeOf(Status status);
        MaskKey                 closeMaskOf(const HttpSession& session);
        bool                    isInOrder(const HttpSession& session, FrameOpcode opcode);
        KiottyNetHttpStatus     storePiece(HttpSession& session, Span<const char> piece);
        void                    storeControl(HttpSession& session, Span<const char> piece);
        void                    deliver(HttpSession& session,
                                        FrameOpcode opcode,
                                        KiottyNetSpan data);
    }

    KiottyNetTransition onSocketWritten(Context context, Connection, Event event)
    {
        HttpSession& session = sessionOf(context);
        session.sent += event.length;
        const PieceResult piece = pieceAt(session, session.sent);
        if (!piece) { return closeWith(piece.code); }
        if (piece.value.empty()) { return openSocket(session); }
        const KiottyNetSpan data{ piece.value.data, piece.value.length };
        return Next::moveToNextState(HttpState::SOCKET_OPENING, thenWrite(data, 0));
    }

    KiottyNetTransition onFrameStart(Context context, Connection, Event event)
    {
        HttpSession& session = sessionOf(context);
        const FrameStartResult parsed = parseFrameStart(spanOf(event.data));
        if (!parsed) { return failSocket(session, parsed.code); }
        const FrameStart& start = parsed.value;
        const bool is_masked = start.masking == FrameMasking::MASKED;
        const bool is_server = session.site.role == SocketRole::SERVER;
        const bool is_valid = is_masked == is_server && isInOrder(session, start.opcode);
        if (!is_valid) { return failSocket(session, PROTOCOL_ERROR); }
        session.socket.frame = SocketFrame{
            0, 0, MaskKey{}, start.opcode, start.finality, start.masking,
        };
        const size_t extended = extendedLengthBytesOf(start.length_code);
        if (extended > 0) { return readNext(session, HttpState::FRAME_LENGTH, extended); }
        return afterLength(session, start.length_code);
    }

    KiottyNetTransition onFrameLength(Context context, Connection, Event event)
    {
        HttpSession& session = sessionOf(context);
        const FrameLengthResult length = parseExtendedLength(spanOf(event.data));
        if (!length) { return failSocket(session, length.code); }
        return afterLength(session, length.value);
    }

    KiottyNetTransition onFrameMask(Context context, Connection, Event event)
    {
        HttpSession& session = sessionOf(context);
        session.socket.frame.key = parseMaskKey(spanOf(event.data));
        return beginPayload(session);
    }

    KiottyNetTransition onFramePayload(Context context, Connection, Event event)
    {
        HttpSession& session = sessionOf(context);
        SocketFrame& frame = session.socket.frame;
        const Span<const char> piece = spanOf(event.data);
        const KiottyNetHttpStatus status = storePiece(session, piece);
        if (status != NO_ERROR_STATUS) { return failSocket(session, TOO_BIG); }
        frame.offset += piece.length;
        frame.remaining -= piece.length;
        if (frame.remaining > 0) { return readPayload(session); }
        if (isControl(frame.opcode)) { return finishControl(session); }
        return finishData(session);
    }

    KiottyNetTransition onSocketTimeout(Context context, Connection, Event)
    {
        HttpSession& session = sessionOf(context);
        if (isClosing(session)) { return closeWith(KIOTTY_NET_CODE_TIMEOUT); }
        const Span<const char> empty{ nullptr, 0 };
        const bool is_pong_late = session.socket.ping_deadline != 0;
        if (is_pong_late) { (void)pushClose(*session.site.lane, session.link, GOING_AWAY, empty); }
        else
        {
            (void)pushFrame(*session.site.lane, session.link, FrameOpcode::PING, empty);
            const uint32_t pong_timeout = session.site.limits.websocket_pong_timeout_milliseconds;
            session.socket.ping_deadline = deadlineAfter(pong_timeout);
        }
        const SocketRead read = session.socket.read;
        return readNext(session, static_cast<HttpState>(read.state), read.length);
    }

    KiottyNetTransition openSocket(HttpSession& session)
    {
        const KiottyNetConnectionHandle connection = session.request.request().connection;
        session.request.begin(connection);
        session.phase = SessionPhase::SOCKET;
        session.socket.is_open = true;
        const WebSocketTarget& target = session.socket.target;
        session.link.connection.store(connection.id);
        session.link.path.store(target.path);
        session.link.closing.store(0);
        session.link.socket.store(session.id.value());
        const KiottyNetWebSocketHandle handle = socketHandleOf(session);
        session.socket.context = target.handler.onOpen(target.app_context, handle);
        Parker* opened = session.site.upgrade.opened;
        const auto wake = []() {};
        if (opened != nullptr) { opened->unpark(wake); }
        return nextFrame(session);
    }

    void endSocket(HttpSession& session, KiottyNetCode reason)
    {
        if (!session.socket.is_open) { return; }
        session.socket.is_open = false;
        session.link.socket.store(0);
        const KiottyNetWebSocketClose close = closeOf(session, reason);
        const WebSocketTarget& target = session.socket.target;
        const KiottyNetWebSocketHandle handle = socketHandleOf(session);
        if (target.handler.onClose != nullptr)
        {
            target.handler.onClose(target.app_context, session.socket.context, handle, close);
        }
        session.link.path.store(NO_SOCKET_PATH);
    }

    namespace
    {
        Span<const char> spanOf(KiottyNetSpan data)
        {
            return Span<const char>{ data.data, data.length };
        }

        KiottyNetTransition readNext(HttpSession& session, HttpState state, size_t length)
        {
            session.socket.read = SocketRead{ length, static_cast<int32_t>(state) };
            const uint32_t timeout = readTimeoutOf(session);
            return Next::moveToNextState(state, thenRead(length, timeout));
        }

        KiottyNetTransition nextFrame(HttpSession& session)
        {
            return readNext(session, HttpState::FRAME_START, FRAME_START_BYTES);
        }

        KiottyNetTransition afterLength(HttpSession& session, uint64_t length)
        {
            SocketFrame& frame = session.socket.frame;
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
            if (isControl(session.socket.frame.opcode)) { session.socket.control_length = 0; }
            if (session.socket.frame.remaining > 0) { return readPayload(session); }
            if (isControl(session.socket.frame.opcode)) { return finishControl(session); }
            return finishData(session);
        }

        KiottyNetTransition readPayload(HttpSession& session)
        {
            const uint64_t remaining = session.socket.frame.remaining;
            const uint64_t capacity = session.site.receive_capacity;
            const uint64_t step = remaining < capacity ? remaining : capacity;
            return readNext(session, HttpState::FRAME_PAYLOAD, static_cast<size_t>(step));
        }

        KiottyNetTransition finishControl(HttpSession& session)
        {
            const Span<const char> payload{ session.socket.control, session.socket.control_length };
            const FrameOpcode opcode = session.socket.frame.opcode;
            if (opcode == FrameOpcode::CLOSE) { return answerClose(session); }
            if (opcode == FrameOpcode::PONG) { session.socket.ping_deadline = 0; }
            const bool is_ping = opcode == FrameOpcode::PING;
            if (is_ping && !isClosing(session))
            {
                (void)pushFrame(*session.site.lane, session.link, FrameOpcode::PONG, payload);
            }
            return nextFrame(session);
        }

        KiottyNetTransition finishData(HttpSession& session)
        {
            const SocketFrame& frame = session.socket.frame;
            const bool is_first = frame.opcode != FrameOpcode::CONTINUATION;
            if (is_first) { session.socket.message = frame.opcode; }
            if (frame.finality == FrameFinality::MORE) { return nextFrame(session); }
            const FrameOpcode message = session.socket.message;
            session.socket.message = FrameOpcode::CONTINUATION;
            const KiottyNetHttpRequest& request = session.request.request();
            const KiottyNetConnectionHandle connection = request.connection;
            const bool is_text = message == FrameOpcode::TEXT;
            const bool is_broken = is_text && !isUtf8(spanOf(request.body));
            if (is_broken) { return failSocket(session, INVALID_PAYLOAD); }
            if (!isClosing(session)) { deliver(session, message, request.body); }
            session.request.begin(connection);
            return nextFrame(session);
        }

        KiottyNetTransition answerClose(HttpSession& session)
        {
            const Span<const char> payload{ session.socket.control, session.socket.control_length };
            const ClosePayloadResult parsed = parseClosePayload(payload);
            if (!parsed) { return failSocket(session, parsed.code); }
            session.socket.has_peer_close = true;
            session.socket.peer_status = parsed.value.status;
            uint16_t open = 0;
            const uint16_t echoed = parsed.value.status;
            const bool is_first = session.link.closing.compare_exchange_strong(open, echoed);
            if (!is_first) { return closeWith(KIOTTY_NET_CODE_SUCCESS); }
            const size_t status_bytes = echoed == NO_STATUS ? 0 : STATUS_BYTES;
            const Span<const char> echo{ payload.data, status_bytes };
            return writeClose(session, echo, KIOTTY_NET_CODE_SUCCESS);
        }

        KiottyNetTransition failSocket(HttpSession& session, Status failure)
        {
            const uint16_t status = static_cast<uint16_t>(failure);
            const KiottyNetCode code = codeOf(failure);
            uint16_t open = 0;
            const bool is_first = session.link.closing.compare_exchange_strong(open, status);
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
            const bool is_client = session.site.role == SocketRole::CLIENT;
            const FrameOpcode close = FrameOpcode::CLOSE;
            const FrameHead head = is_client ? buildMaskedFrameHead(close, payload.length, key)
                                             : buildFrameHead(close, payload.length);
            const Span<char> outgoing{ session.socket.outgoing, CLOSE_FRAME_BYTES };
            (void)outgoing.copyFrom(head.span());
            const Span<char> body{ session.socket.outgoing + head.length, payload.length };
            (void)body.copyFrom(payload);
            applyMask(body, key, 0);
            const KiottyNetSpan data{ session.socket.outgoing, head.length + payload.length };
            const KiottyNetConnectionAction farewell = thenCloseWith(data, code);
            return Next::moveToNextState(HttpState::CLOSING, farewell);
        }

        MaskKey closeMaskOf(const HttpSession& session)
        {
            MaskKey key{};
            if (session.site.role == SocketRole::SERVER) { return key; }
            const uint64_t seed = deadlineAfter(1) * GOLDEN_GAMMA;
            for (size_t i = 0; i < MASK_KEY_BYTES; ++i)
            {
                key.bytes[i] = static_cast<uint8_t>(seed >> (BITS_PER_BYTE * (i + 4)));
            }
            return key;
        }

        KiottyNetCode codeOf(Status status)
        {
            if (status == TOO_BIG) { return KIOTTY_NET_CODE_REQUEST_TOO_LARGE; }
            return KIOTTY_NET_CODE_PROTOCOL_ERROR;
        }

        bool isInOrder(const HttpSession& session, FrameOpcode opcode)
        {
            if (isControl(opcode)) { return true; }
            const bool is_in_message = session.socket.message != FrameOpcode::CONTINUATION;
            const bool is_continuation = opcode == FrameOpcode::CONTINUATION;
            return is_in_message == is_continuation;
        }

        KiottyNetHttpStatus storePiece(HttpSession& session, Span<const char> piece)
        {
            const SocketFrame& frame = session.socket.frame;
            if (!isControl(frame.opcode))
            {
                return session.request.appendBody(piece, frame.key, frame.offset);
            }
            storeControl(session, piece);
            return NO_ERROR_STATUS;
        }

        void storeControl(HttpSession& session, Span<const char> piece)
        {
            const size_t offset = session.socket.frame.offset;
            const Span<char> target{ session.socket.control + offset, piece.length };
            (void)target.copyFrom(piece);
            applyMask(target, session.socket.frame.key, offset);
            session.socket.control_length = offset + piece.length;
        }

        void deliver(HttpSession& session, FrameOpcode opcode, KiottyNetSpan data)
        {
            const bool is_text = opcode == FrameOpcode::TEXT;
            const KiottyNetWebSocketOpcode kind =
                is_text ? KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT : KIOTTY_NET_WEB_SOCKET_OPCODE_BINARY;
            const KiottyNetWebSocketMessage message{ kind, data };
            const WebSocketTarget& target = session.socket.target;
            target.handler.onMessage(session.socket.context, socketHandleOf(session), message);
        }
    }
}
