#include "http/data/repository/machine/kiotty_http_exchange.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_body_plan_rules.h"
#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/machine/kiotty_http_answer_writer.h"
#include "http/data/repository/response/kiotty_http_answer_rules.h"
#include "http/data/repository/response/kiotty_http_send_rules.h"
#include "http/data/repository/server/kiotty_http_session_host.h"
#include "http/domain/entity/kiotty_http_constants.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;

        constexpr KiottyNetSpan LINE_END_SPAN{ LINE_END, 2 };
        constexpr int32_t       SERVER_ERROR = KIOTTY_NET_HTTP_STATUS_INTERNAL_SERVER_ERROR;

        size_t                      lineLimitOf(const HttpSessionHost& host);
        KiottyNetHttpResponseHandle responseHandleOf(const HttpSession& session);
        KiottyNetTransition callRoute(HttpSession& session, const KiottyNetHttpRoute& route);
        KiottyNetTransition         afterRoute(HttpSession& session);
        RouteOutcome                outcomeOf(const HttpSession& session);
        KiottyNetTransition         awaitWork(HttpSession& session);
        void                        answerWithoutWork(HttpSession& session);
        KiottyNetTransition sendPiece(const HttpSession& session, const SendStep& step);
    }

    HttpSession& sessionOf(KiottyNetConnectionContext context)
    {
        return *static_cast<HttpSession*>(context);
    }

    Span<const char> lineOf(KiottyNetSpan data)
    {
        const bool has_line_end = data.length >= LINE_END_SPAN.length;
        const size_t length = has_line_end ? data.length - LINE_END_SPAN.length : data.length;
        return Span<const char>{ data.data, length };
    }

    KiottyNetTransition beginExchange(HttpSession& session,
                                      KiottyNetConnectionHandle connection,
                                      uint32_t wait_milliseconds)
    {
        advanceSession(session);
        detachFile(session);
        session.request.begin(connection);
        session.response.begin();
        session.stream = KiottyNetConnectionStream{ nullptr, 0 };
        session.state = idleSessionState();
        return readLine(session, HttpState::REQUEST_LINE, wait_milliseconds);
    }

    KiottyNetTransition readLine(HttpSession& session,
                                 HttpState state,
                                 uint32_t timeout_milliseconds)
    {
        const size_t limit = lineLimitOf(session.host);
        const uint32_t timeout = timeout_milliseconds;
        const KiottyNetConnectionAction action = thenReadUntil(LINE_END_SPAN, limit, timeout);
        return Next::moveToNextState(state, action);
    }

    KiottyNetTransition readBody(HttpSession& session, HttpState state)
    {
        const size_t step = nextReadOf(session.state.body, session.host.receive_capacity);
        const uint32_t timeout = session.host.limits.header_timeout_milliseconds;
        const KiottyNetConnectionAction action = thenRead(step, timeout);
        return Next::moveToNextState(state, action);
    }

    KiottyNetTransition reject(HttpSession& session, KiottyNetHttpStatus status)
    {
        session.response.begin();
        (void)session.response.setStatus(status);
        session.state.persistence = Persistence::CLOSE;
        session.state.close_reason = KIOTTY_NET_CODE_PROTOCOL_ERROR;
        return startSending(session);
    }

    KiottyNetTransition dispatch(HttpSession& session)
    {
        session.state.phase = SessionPhase::FILLING;
        const KiottyNetHttpRequest& request = session.request.request();
        const Span<const char> path{ request.path.data, request.path.length };
        const RouteMatch match = session.host.registry.find(request.method, path);
        const bool is_route = match.kind == MatchKind::ROUTE;
        if (is_route) { return callRoute(session, match.route); }
        HttpUpgradeHandler* const upgrade = session.host.upgrade;
        const bool is_upgrade = match.kind == MatchKind::UPGRADE && upgrade != nullptr;
        if (is_upgrade) { return upgrade->answer(session, match.upgrade); }
        const Answer answer = answerOf(match, path);
        writeAnswer(session, answer);
        return startSending(session);
    }

    KiottyNetTransition startWork(HttpSession& session, KiottyNetConnectionStream stream)
    {
        session.stream = stream;
        const KiottyNetHttpRoute route = session.route;
        const KiottyNetHttpRequest* request = &session.request.request();
        const KiottyNetHttpResponseHandle response = responseHandleOf(session);
        const KiottyNetHttpWorkHandle work{ session.host.owner, session.id.value() };
        route.onWorkStarted(session.host.app_context, request, response, work);
        return Next::moveToNextState(HttpState::WORKING, thenWork());
    }

    KiottyNetTransition sendNext(HttpSession& session, KiottyNetConnectionHandle connection)
    {
        const PieceResult piece = pieceAt(session, session.state.sent);
        if (!piece) { return closeWith(piece.code); }
        const ResponseFrame frame = session.response.frame();
        const HttpSessionState& state = session.state;
        const SendStep step = sendStepOf(frame, state.sent, piece.value, state.persistence);
        if (step.kind != SendKind::DONE) { return sendPiece(session, step); }
        const AfterResponse after = afterResponseOf(session.state.persistence);
        if (after == AfterResponse::CLOSE) { return closeWith(session.state.close_reason); }
        const uint32_t wait = session.host.limits.keep_alive_timeout_milliseconds;
        return beginExchange(session, connection, wait);
    }

    KiottyNetTransition startSending(HttpSession& session)
    {
        session.state.phase = SessionPhase::SENDING;
        sealResponse(session);
        session.state.sent = 0;
        const ResponseFrame frame = session.response.frame();
        const Span<const char> chunk = frame.chunkAt(0, session.host.send_capacity);
        const SendStep step = sendStepOf(frame, 0, chunk, session.state.persistence);
        return sendPiece(session, step);
    }

    void sealResponse(HttpSession& session)
    {
        const KiottyNetHttpMethod method = session.request.request().method;
        session.response.seal(ResponseSeal{ method, session.state.persistence });
    }

    KiottyNetTransition closeWith(KiottyNetCode reason)
    {
        return Next::moveToNextState(HttpState::CLOSING, thenClose(reason));
    }

    void startHeaderClock(HttpSession& session)
    {
        const uint32_t timeout = session.host.limits.header_timeout_milliseconds;
        session.state.header_deadline = timeout == 0 ? 0 : monotonicMilliseconds() + timeout;
    }

    uint32_t headerTimeLeft(const HttpSession& session)
    {
        if (session.state.header_deadline == 0) { return 0; }
        const uint64_t now = monotonicMilliseconds();
        if (now >= session.state.header_deadline) { return EXPIRED_MILLISECONDS; }
        const uint64_t left = session.state.header_deadline - now;
        const uint64_t largest = std::numeric_limits<uint32_t>::max();
        return static_cast<uint32_t>(left < largest ? left : largest);
    }

    namespace
    {
        size_t lineLimitOf(const HttpSessionHost& host)
        {
            const size_t header_line = host.limits.max_header_bytes + LINE_END_SPAN.length;
            return host.receive_capacity < header_line ? host.receive_capacity : header_line;
        }

        KiottyNetHttpResponseHandle responseHandleOf(const HttpSession& session)
        {
            return KiottyNetHttpResponseHandle{ session.host.owner, session.id.value() };
        }

        KiottyNetTransition callRoute(HttpSession& session, const KiottyNetHttpRoute& route)
        {
            session.route = route;
            const KiottyNetHttpRequest& request = session.request.request();
            const KiottyNetAppContext app_context = session.host.app_context;
            route.onRequest(app_context, &request, responseHandleOf(session));
            return afterRoute(session);
        }

        KiottyNetTransition afterRoute(HttpSession& session)
        {
            const RouteOutcome outcome = outcomeOf(session);
            const AfterRoute next = afterRouteOf(outcome);
            if (next == AfterRoute::AWAIT_WORK) { return awaitWork(session); }
            if (next == AfterRoute::REFUSE_STREAM) { answerWithoutWork(session); }
            return startSending(session);
        }

        RouteOutcome outcomeOf(const HttpSession& session)
        {
            const bool is_stream = session.response.frame().isStream();
            const bool is_head = session.request.request().method == KIOTTY_NET_HTTP_METHOD_HEAD;
            return RouteOutcome{ is_stream, is_head, session.state.is_work_requested };
        }

        KiottyNetTransition awaitWork(HttpSession& session)
        {
            session.state.phase = SessionPhase::WORKING;
            session.is_worker_owned = true;
            return Next::moveToNextState(HttpState::WORKING, thenWork());
        }

        void answerWithoutWork(HttpSession& session)
        {
            session.response.begin();
            (void)session.response.setStatus(SERVER_ERROR);
            session.state.persistence = Persistence::CLOSE;
        }

        KiottyNetTransition sendPiece(const HttpSession& session, const SendStep& step)
        {
            const KiottyNetSpan data{ step.chunk.data, step.chunk.length };
            const bool is_farewell = step.kind == SendKind::FAREWELL;
            const KiottyNetConnectionAction write = thenWrite(data, 0);
            if (!is_farewell) { return Next::moveToNextState(HttpState::SENDING, write); }
            const KiottyNetCode reason = session.state.close_reason;
            const KiottyNetConnectionAction farewell = thenCloseWith(data, reason);
            return Next::moveToNextState(HttpState::CLOSING, farewell);
        }
    }
}
