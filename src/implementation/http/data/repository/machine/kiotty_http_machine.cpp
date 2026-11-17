#include "http/data/repository/machine/kiotty_http_machine.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/repository/body/kiotty_http_body_plan_rules.h"
#include "http/data/repository/body/kiotty_http_message_framing_parser.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "http/data/repository/machine/kiotty_http_rule_names.h"
#include "http/domain/entity/kiotty_http_constants.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Event = KiottyNetConnectionEvent;
        using Session = KiottyNetConnectionContext;
        using Handle = KiottyNetConnectionHandle;
        using EventType = KiottyNetConnectionEventType;

        constexpr size_t CHUNK_END_BYTES = 2;
        constexpr char          CONTINUE_LINE[] = "HTTP/1.1 100 Continue\r\n\r\n";
        constexpr KiottyNetSpan CONTINUE_SPAN{ CONTINUE_LINE, sizeof(CONTINUE_LINE) - 1 };

        KiottyNetTransition onConnected(Session context, Handle connection, Event event);
        KiottyNetTransition onRequestLine(Session context, Handle connection, Event event);
        KiottyNetTransition onHeaderLine(Session context, Handle connection, Event event);
        KiottyNetTransition onBody(Session context, Handle connection, Event event);
        KiottyNetTransition onChunkSize(Session context, Handle connection, Event event);
        KiottyNetTransition onChunkData(Session context, Handle connection, Event event);
        KiottyNetTransition onChunkEnd(Session context, Handle connection, Event event);
        KiottyNetTransition onTrailer(Session context, Handle connection, Event event);
        KiottyNetTransition onContinued(Session context, Handle connection, Event event);
        KiottyNetTransition onWorkStarted(Session context, Handle connection, Event event);
        KiottyNetTransition onSent(Session context, Handle connection, Event event);
        KiottyNetTransition onUpgraded(Session context, Handle connection, Event event);
        KiottyNetTransition onTimeout(Session context, Handle connection, Event event);
        KiottyNetTransition onClosed(Session context, Handle connection, Event event);

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
        constexpr HttpState BODY = HttpState::BODY;
        constexpr HttpState CHUNK_SIZE = HttpState::CHUNK_SIZE;
        constexpr HttpState CHUNK_DATA = HttpState::CHUNK_DATA;
        constexpr HttpState CHUNK_END = HttpState::CHUNK_END;
        constexpr HttpState TRAILER = HttpState::TRAILER;
        constexpr HttpState CONTINUING = HttpState::CONTINUING;
        constexpr HttpState WORKING = HttpState::WORKING;
        constexpr HttpState SENDING = HttpState::SENDING;

        constexpr EventType WORK_STARTED = KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED;

        constexpr auto HTTP_RULES = KiottyNet_rules(
            KiottyNet_when(CONNECTED).then(onConnected).to(REQUEST_LINE),
            KiottyNet_from(REQUEST_LINE)
                .when(READ)
                .then(onRequestLine)
                .to(HEADER_LINE, SENDING, CLOSING),
            KiottyNet_from(HEADER_LINE)
                .when(READ)
                .then(onHeaderLine)
                .to(HEADER_LINE,
                    BODY,
                    CHUNK_SIZE,
                    CONTINUING,
                    SENDING,
                    WORKING,
                    UPGRADED,
                    CLOSING),
            KiottyNet_from(BODY).when(READ).then(onBody).to(BODY, SENDING, WORKING, CLOSING),
            KiottyNet_from(CHUNK_SIZE)
                .when(READ)
                .then(onChunkSize)
                .to(CHUNK_DATA, TRAILER, SENDING, CLOSING),
            KiottyNet_from(CHUNK_DATA)
                .when(READ)
                .then(onChunkData)
                .to(CHUNK_DATA, CHUNK_END, SENDING, CLOSING),
            KiottyNet_from(CHUNK_END).when(READ).then(onChunkEnd).to(CHUNK_SIZE, SENDING, CLOSING),
            KiottyNet_from(TRAILER)
                .when(READ)
                .then(onTrailer)
                .to(TRAILER, SENDING, WORKING, CLOSING),
            KiottyNet_from(CONTINUING).when(WRITTEN).then(onContinued).to(BODY, CHUNK_SIZE),
            KiottyNet_from(WORKING).when(WORK_STARTED).then(onWorkStarted).to(WORKING),
            KiottyNet_from(WORKING).when(WRITTEN).then(onSent).to(SENDING, REQUEST_LINE, CLOSING),
            KiottyNet_from(WORKING)
                .when(READ)
                .then(onRequestLine)
                .to(HEADER_LINE, SENDING, CLOSING),
            KiottyNet_from(SENDING).when(WRITTEN).then(onSent).to(SENDING, REQUEST_LINE, CLOSING),
            KiottyNet_from(UPGRADED).when(WRITTEN).then(onUpgraded).to(UPGRADED, CLOSING),
            KiottyNet_from(UPGRADED).when(READ).then(onUpgraded).to(UPGRADED, CLOSING),
            KiottyNet_from(UPGRADED).when(TIMEOUT).then(onUpgraded).to(UPGRADED, CLOSING),
            KiottyNet_when(TIMEOUT).then(onTimeout).to(CLOSING),
            KiottyNet_when(CLOSED).then(onClosed));

        constexpr KiottyNetStateMachine HTTP_MACHINE =
            KiottyNet_makeStateMachine(HTTP_RULES, REQUEST_LINE);
    }

    KiottyNetStatefulAppContext httpStatefulOf(HttpSessionHost& host)
    {
        return KiottyNetStatefulAppContext{
            &host, &HTTP_MACHINE, createSession, releaseSession, reportError,
        };
    }

    namespace
    {
        KiottyNetTransition onConnected(Session context, Handle connection, Event)
        {
            HttpSession& session = sessionOf(context);
            const uint32_t wait = session.host.limits.header_timeout_milliseconds;
            return beginExchange(session, connection, wait);
        }

        KiottyNetTransition onRequestLine(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            startHeaderClock(session);
            const KiottyNetHttpStatus status = session.request.takeRequestLine(lineOf(event.data));
            if (status != NO_ERROR_STATUS) { return reject(session, status); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onHeaderLine(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            const Span<const char> line = lineOf(event.data);
            if (line.empty()) { return afterHeaders(session); }
            const KiottyNetHttpStatus status = session.request.takeHeader(line);
            if (status != NO_ERROR_STATUS) { return reject(session, status); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onBody(Session context, Handle, Event event)
        {
            return takeBody(sessionOf(context), event.data, BODY);
        }

        KiottyNetTransition onChunkSize(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            const LengthResult size = parseChunkSize(lineOf(event.data));
            if (!size) { return reject(session, size.code); }
            if (size.value == 0) { return readLine(session, TRAILER, bodyTimeOf(session)); }
            const uint64_t used = session.request.request().body.length;
            const uint64_t room = session.host.limits.max_body_bytes - used;
            const BodyPlanResult plan = planBody(size.value, room);
            if (!plan) { return reject(session, plan.code); }
            session.state.body = plan.value;
            return readBody(session, CHUNK_DATA);
        }

        KiottyNetTransition onChunkData(Session context, Handle, Event event)
        {
            return takeBody(sessionOf(context), event.data, CHUNK_DATA);
        }

        KiottyNetTransition onChunkEnd(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            const bool has_two_bytes = event.data.length == CHUNK_END_BYTES;
            const bool is_line_end = has_two_bytes && lineOf(event.data).empty();
            if (!is_line_end) { return reject(session, KIOTTY_NET_HTTP_STATUS_BAD_REQUEST); }
            return readLine(session, CHUNK_SIZE, bodyTimeOf(session));
        }

        KiottyNetTransition onTrailer(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            if (lineOf(event.data).empty()) { return dispatch(session); }
            return readLine(session, TRAILER, bodyTimeOf(session));
        }

        KiottyNetTransition onContinued(Session context, Handle, Event)
        {
            return readFirstBody(sessionOf(context));
        }

        KiottyNetTransition onWorkStarted(Session context, Handle, Event event)
        {
            return startWork(sessionOf(context), event.stream);
        }

        KiottyNetTransition onSent(Session context, Handle connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            session.state.sent += event.length;
            return sendNext(session, connection);
        }

        KiottyNetTransition onUpgraded(Session context, Handle connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            return session.host.upgrade->step(session, connection, event);
        }

        KiottyNetTransition onTimeout(Session, Handle, Event)
        {
            return closeWith(KIOTTY_NET_CODE_TIMEOUT);
        }

        KiottyNetTransition onClosed(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            HttpUpgradeHandler* const upgrade = session.host.upgrade;
            if (upgrade != nullptr) { upgrade->end(session, event.reason); }
            const KiottyNetAppContext app_context = session.host.app_context;
            session.host.notices.reportDisconnected(app_context, session.info, event.reason);
            return Next::moveToNextState(CLOSING, thenHold(0));
        }

        KiottyNetTransition afterHeaders(HttpSession& session)
        {
            const RequestFramingResult framing = session.request.finishHeaders();
            if (!framing) { return reject(session, framing.code); }
            session.state.framing = framing.value;
            session.state.persistence = framing.value.persistence;
            const BodyFraming& body = framing.value.body;
            switch (body.kind)
            {
            case BodyKind::NONE:
                return dispatch(session);
            case BodyKind::LENGTH:
                return afterLength(session, body.length);
            case BodyKind::CHUNKED:
                return beginBody(session);
            }
            return dispatch(session);
        }

        KiottyNetTransition afterLength(HttpSession& session, uint64_t length)
        {
            const BodyPlanResult plan = planBody(length, session.host.limits.max_body_bytes);
            if (!plan) { return reject(session, plan.code); }
            if (!hasMoreBody(plan.value)) { return dispatch(session); }
            session.state.body = plan.value;
            return beginBody(session);
        }

        KiottyNetTransition beginBody(HttpSession& session)
        {
            if (!session.state.framing.expects_continue) { return readFirstBody(session); }
            return Next::moveToNextState(CONTINUING, thenWrite(CONTINUE_SPAN, 0));
        }

        KiottyNetTransition readFirstBody(HttpSession& session)
        {
            if (hasMoreBody(session.state.body)) { return readBody(session, BODY); }
            return readLine(session, CHUNK_SIZE, bodyTimeOf(session));
        }

        KiottyNetTransition takeBody(HttpSession& session, KiottyNetSpan data, HttpState more)
        {
            const Span<const char> piece{ data.data, data.length };
            const AppendedBodyResult appended = session.request.appendBody(piece);
            if (!appended) { return reject(session, appended.code); }
            session.state.body = takeBodyPiece(session.state.body, data.length);
            if (hasMoreBody(session.state.body)) { return readBody(session, more); }
            const bool is_chunk = more == CHUNK_DATA;
            const uint32_t timeout = bodyTimeOf(session);
            const KiottyNetConnectionAction line_end = thenRead(CHUNK_END_BYTES, timeout);
            if (is_chunk) { return Next::moveToNextState(CHUNK_END, line_end); }
            return dispatch(session);
        }

        uint32_t bodyTimeOf(const HttpSession& session)
        {
            return session.host.limits.header_timeout_milliseconds;
        }

        KiottyNetConnectionContext createSession(KiottyNetAppContext app_context,
                                                 KiottyNetConnectionInfo info)
        {
            HttpSessionHost& host = *static_cast<HttpSessionHost*>(app_context);
            const SessionResult acquired = host.sessions.acquire(info);
            if (!acquired) { return nullptr; }
            return &acquired.value();
        }

        void releaseSession(KiottyNetAppContext app_context, KiottyNetConnectionContext context)
        {
            HttpSessionHost& host = *static_cast<HttpSessionHost*>(app_context);
            host.sessions.release(sessionOf(context));
        }

        void reportError(KiottyNetAppContext app_context,
                         KiottyNetConnectionInfo info,
                         KiottyNetCode code,
                         const char* message)
        {
            const HttpSessionHost& host = *static_cast<HttpSessionHost*>(app_context);
            host.notices.reportError(host.app_context, info, code, message);
        }
    }
}
