#include "domain/usecases/http/kiotty_http_machine.h"

#include "domain/entity/kiotty_action.h"
#include "domain/usecases/http/kiotty_http_exchange.h"
#include "domain/usecases/http/kiotty_http_message_framing.h"
#include "domain/usecases/websocket/kiotty_websocket_rows.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Event = KiottyNetConnectionEvent;
        using Context = KiottyNetConnectionContext;
        using Connection = KiottyNetConnectionHandle;
        using EventType = KiottyNetConnectionEventType;

        constexpr size_t CHUNK_END_BYTES = 2;
        constexpr KiottyNetHttpStatus CONTENT_TOO_LARGE = KIOTTY_NET_HTTP_STATUS_CONTENT_TOO_LARGE;
        constexpr char          CONTINUE_LINE[] = "HTTP/1.1 100 Continue\r\n\r\n";
        constexpr KiottyNetSpan CONTINUE_SPAN{ CONTINUE_LINE, sizeof(CONTINUE_LINE) - 1 };

        KiottyNetTransition onConnected(Context context, Connection connection, Event event);
        KiottyNetTransition onRequestLine(Context context, Connection connection, Event event);
        KiottyNetTransition onHeaderLine(Context context, Connection connection, Event event);
        KiottyNetTransition onBody(Context context, Connection connection, Event event);
        KiottyNetTransition onChunkSize(Context context, Connection connection, Event event);
        KiottyNetTransition onChunkData(Context context, Connection connection, Event event);
        KiottyNetTransition onChunkEnd(Context context, Connection connection, Event event);
        KiottyNetTransition onTrailer(Context context, Connection connection, Event event);
        KiottyNetTransition onContinued(Context context, Connection connection, Event event);
        KiottyNetTransition onWorkStarted(Context context, Connection connection, Event event);
        KiottyNetTransition onSent(Context context, Connection connection, Event event);
        KiottyNetTransition onTimeout(Context context, Connection connection, Event event);
        KiottyNetTransition onClosed(Context context, Connection connection, Event event);

        KiottyNetTransition afterHeaders(HttpSession& session);
        KiottyNetTransition afterLength(HttpSession& session, uint64_t length);
        KiottyNetTransition beginBody(HttpSession& session);
        KiottyNetTransition readFirstBody(HttpSession& session);
        KiottyNetTransition takeBody(HttpSession& session, KiottyNetSpan data, HttpState more);
        uint32_t            bodyTimeOf(const HttpSession& session);

        KiottyNetConnectionContext createSession(KiottyNetAppContext app_context,
                                                 KiottyNetConnectionInfo info);
        void                       releaseSession(KiottyNetAppContext app_context,
                                                  KiottyNetConnectionContext context);
        void                       reportError(KiottyNetAppContext app_context,
                                               KiottyNetConnectionInfo info,
                                               KiottyNetCode code,
                                               const char* message);

        constexpr HttpState REQUEST_LINE = HttpState::REQUEST_LINE;
        constexpr HttpState HEADER_LINE = HttpState::HEADER_LINE;
        constexpr HttpState BODY = HttpState::BODY;
        constexpr HttpState CHUNK_SIZE = HttpState::CHUNK_SIZE;
        constexpr HttpState CHUNK_DATA = HttpState::CHUNK_DATA;
        constexpr HttpState CHUNK_END = HttpState::CHUNK_END;
        constexpr HttpState TRAILER = HttpState::TRAILER;
        constexpr HttpState CONTINUING = HttpState::CONTINUING;
        constexpr HttpState WORKING = HttpState::WORKING;
        constexpr HttpState SENDING = HttpState::SENDING;
        constexpr HttpState CLOSING = HttpState::CLOSING;
        constexpr HttpState SOCKET_OPENING = HttpState::SOCKET_OPENING;
        constexpr HttpState FRAME_START = HttpState::FRAME_START;
        constexpr HttpState FRAME_LENGTH = HttpState::FRAME_LENGTH;
        constexpr HttpState FRAME_MASK = HttpState::FRAME_MASK;
        constexpr HttpState FRAME_PAYLOAD = HttpState::FRAME_PAYLOAD;
        constexpr HttpState SOCKET_CLOSING = HttpState::SOCKET_CLOSING;

        constexpr EventType CONNECTED = KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED;
        constexpr EventType READ = KIOTTY_NET_CONNECTION_EVENT_TYPE_READ;
        constexpr EventType WRITTEN = KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN;
        constexpr EventType WORK_STARTED = KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED;
        constexpr EventType TIMEOUT = KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT;
        constexpr EventType CLOSED = KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED;

        constexpr auto HTTP_RULES = KiottyNet_rules(
            KiottyNet_when(CONNECTED).then(onConnected).to(REQUEST_LINE),
            KiottyNet_from(REQUEST_LINE).when(READ).then(onRequestLine).to(HEADER_LINE, SENDING),
            KiottyNet_from(HEADER_LINE)
                .when(READ)
                .then(onHeaderLine)
                .to(HEADER_LINE, BODY, CHUNK_SIZE, CONTINUING, SENDING, WORKING, SOCKET_OPENING),
            KiottyNet_from(BODY).when(READ).then(onBody).to(BODY, SENDING, WORKING),
            KiottyNet_from(CHUNK_SIZE)
                .when(READ)
                .then(onChunkSize)
                .to(CHUNK_DATA, TRAILER, SENDING),
            KiottyNet_from(CHUNK_DATA)
                .when(READ)
                .then(onChunkData)
                .to(CHUNK_DATA, CHUNK_END, SENDING),
            KiottyNet_from(CHUNK_END).when(READ).then(onChunkEnd).to(CHUNK_SIZE, SENDING),
            KiottyNet_from(TRAILER).when(READ).then(onTrailer).to(TRAILER, SENDING, WORKING),
            KiottyNet_from(CONTINUING).when(WRITTEN).then(onContinued).to(BODY, CHUNK_SIZE),
            KiottyNet_from(WORKING).when(WORK_STARTED).then(onWorkStarted).to(WORKING),
            KiottyNet_from(WORKING).when(WRITTEN).then(onSent).to(SENDING, REQUEST_LINE, CLOSING),
            KiottyNet_from(WORKING).when(READ).then(onRequestLine).to(HEADER_LINE, SENDING),
            KiottyNet_from(SENDING).when(WRITTEN).then(onSent).to(SENDING, REQUEST_LINE, CLOSING),
            KiottyNet_from(SOCKET_OPENING)
                .when(WRITTEN)
                .then(onSocketWritten)
                .to(SOCKET_OPENING, FRAME_START),
            KiottyNet_from(FRAME_START)
                .when(READ)
                .then(onFrameStart)
                .to(FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_LENGTH)
                .when(READ)
                .then(onFrameLength)
                .to(FRAME_MASK, FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_MASK)
                .when(READ)
                .then(onFrameMask)
                .to(FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_PAYLOAD)
                .when(READ)
                .then(onFramePayload)
                .to(FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_START)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_LENGTH)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_MASK)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_PAYLOAD)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(SOCKET_CLOSING).when(WRITTEN).then(onCloseWritten).to(CLOSING),
            KiottyNet_when(TIMEOUT).then(onTimeout).to(CLOSING),
            KiottyNet_when(CLOSED).then(onClosed));

        constexpr KiottyNetStateMachine HTTP_MACHINE =
            KiottyNet_makeStateMachine(HTTP_RULES, REQUEST_LINE);
    }

    KiottyNetStatefulAppContext httpStatefulOf(HttpSite& site)
    {
        return KiottyNetStatefulAppContext{
            &site, &HTTP_MACHINE, createSession, releaseSession, reportError,
        };
    }

    namespace
    {
        KiottyNetTransition onConnected(Context context, Connection connection, Event)
        {
            HttpSession& session = sessionOf(context);
            const uint32_t wait = session.site.limits.header_timeout_milliseconds;
            return beginExchange(session, connection, wait);
        }

        KiottyNetTransition onRequestLine(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            startHeaderClock(session);
            const KiottyNetHttpStatus status = session.request.takeRequestLine(lineOf(event.data));
            if (status != NO_ERROR_STATUS) { return reject(session, status); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onHeaderLine(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            const Span<const char> line = lineOf(event.data);
            if (line.empty()) { return afterHeaders(session); }
            const KiottyNetHttpStatus status = session.request.takeHeader(line);
            if (status != NO_ERROR_STATUS) { return reject(session, status); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onBody(Context context, Connection, Event event)
        {
            return takeBody(sessionOf(context), event.data, BODY);
        }

        KiottyNetTransition onChunkSize(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            const LengthResult size = parseChunkSize(lineOf(event.data));
            if (!size) { return reject(session, size.code); }
            if (size.value == 0) { return readLine(session, TRAILER, bodyTimeOf(session)); }
            const uint64_t used = session.request.request().body.length;
            const uint64_t room = session.site.limits.max_body_bytes - used;
            if (size.value > room) { return reject(session, CONTENT_TOO_LARGE); }
            session.body_remaining = size.value;
            return readBody(session, CHUNK_DATA);
        }

        KiottyNetTransition onChunkData(Context context, Connection, Event event)
        {
            return takeBody(sessionOf(context), event.data, CHUNK_DATA);
        }

        KiottyNetTransition onChunkEnd(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            const bool has_two_bytes = event.data.length == CHUNK_END_BYTES;
            const bool is_line_end = has_two_bytes && lineOf(event.data).empty();
            if (!is_line_end) { return reject(session, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST); }
            return readLine(session, CHUNK_SIZE, bodyTimeOf(session));
        }

        KiottyNetTransition onTrailer(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            if (lineOf(event.data).empty()) { return dispatch(session); }
            return readLine(session, TRAILER, bodyTimeOf(session));
        }

        KiottyNetTransition onContinued(Context context, Connection, Event)
        {
            return readFirstBody(sessionOf(context));
        }

        KiottyNetTransition onWorkStarted(Context context, Connection, Event event)
        {
            return startWork(sessionOf(context), event.stream);
        }

        KiottyNetTransition onSent(Context context, Connection connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            session.sent += event.length;
            return sendNext(session, connection);
        }

        KiottyNetTransition onTimeout(Context, Connection, Event)
        {
            return closeWith(KIOTTY_NET_CODE_TIMEOUT);
        }

        KiottyNetTransition onClosed(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            endSocket(session, event.reason);
            session.site.notices.reportDisconnected(session.info, event.reason);
            return Next::moveToNextState(CLOSING, thenHold(0));
        }

        KiottyNetTransition afterHeaders(HttpSession& session)
        {
            const FramingResult framing = session.request.finishHeaders();
            if (!framing) { return reject(session, framing.code); }
            const bool is_kept = session.request.request().keep_alive != 0;
            session.persistence = is_kept ? Persistence::KEEP_ALIVE : Persistence::CLOSE;
            const bool is_chunked = framing.value.kind == BodyKind::CHUNKED;
            session.has_body = is_chunked || framing.value.length > 0;
            switch (framing.value.kind)
            {
            case BodyKind::NONE:
                return dispatch(session);
            case BodyKind::LENGTH:
                return afterLength(session, framing.value.length);
            case BodyKind::CHUNKED:
                return beginBody(session);
            }
            return dispatch(session);
        }

        KiottyNetTransition afterLength(HttpSession& session, uint64_t length)
        {
            const bool is_too_large = length > session.site.limits.max_body_bytes;
            if (is_too_large) { return reject(session, KIOTTY_NET_HTTP_STATUS_CONTENT_TOO_LARGE); }
            if (length == 0) { return dispatch(session); }
            session.body_remaining = length;
            return beginBody(session);
        }

        KiottyNetTransition beginBody(HttpSession& session)
        {
            if (!session.request.expectsContinue()) { return readFirstBody(session); }
            return Next::moveToNextState(CONTINUING, thenWrite(CONTINUE_SPAN, 0));
        }

        KiottyNetTransition readFirstBody(HttpSession& session)
        {
            if (session.body_remaining > 0) { return readBody(session, BODY); }
            return readLine(session, CHUNK_SIZE, bodyTimeOf(session));
        }

        KiottyNetTransition takeBody(HttpSession& session, KiottyNetSpan data, HttpState more)
        {
            const Span<const char> piece{ data.data, data.length };
            const KiottyNetHttpStatus status = session.request.appendBody(piece);
            if (status != NO_ERROR_STATUS) { return reject(session, status); }
            session.body_remaining -= data.length;
            if (session.body_remaining > 0) { return readBody(session, more); }
            const bool is_chunk = more == CHUNK_DATA;
            const uint32_t timeout = bodyTimeOf(session);
            const KiottyNetConnectionAction line_end = thenRead(CHUNK_END_BYTES, timeout);
            if (is_chunk) { return Next::moveToNextState(CHUNK_END, line_end); }
            return dispatch(session);
        }

        uint32_t bodyTimeOf(const HttpSession& session)
        {
            return session.site.limits.header_timeout_milliseconds;
        }

        KiottyNetConnectionContext createSession(KiottyNetAppContext app_context,
                                                 KiottyNetConnectionInfo info)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            const SessionResult acquired = site.sessions.acquire(info);
            if (!acquired) { return nullptr; }
            return &acquired.value();
        }

        void releaseSession(KiottyNetAppContext app_context, KiottyNetConnectionContext context)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            site.sessions.release(sessionOf(context));
        }

        void reportError(KiottyNetAppContext app_context,
                         KiottyNetConnectionInfo info,
                         KiottyNetCode code,
                         const char* message)
        {
            const HttpSite& site = *static_cast<HttpSite*>(app_context);
            site.notices.reportError(info, code, message);
        }
    }
}
