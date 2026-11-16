#include "http/data/repository/machine/kiotty_http_exchange.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_text.h"
#include "http/data/repository/body/kiotty_http_body_plan_rules.h"
#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/machine/kiotty_http_answer_writer.h"
#include "http/data/repository/response/kiotty_http_answer_rules.h"
#include "http/data/repository/response/kiotty_http_send_rules.h"
#include "http/data/repository/server/kiotty_http_site.h"
#include "http/domain/entity/kiotty_http_constants.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;

        constexpr KiottyNetSpan LINE_END_SPAN{ LINE_END, 2 };
        constexpr int32_t       SERVER_ERROR = KIOTTY_NET_HTTP_STATUS_INTERNAL_SERVER_ERROR;

        size_t                      lineLimitOf(const HttpSite& site);
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
        session.body = BodyPlan{ 0 };
        session.sent = 0;
        session.close_reason = KIOTTY_NET_CODE_SUCCESS;
        session.stream = KiottyNetConnectionStream{ nullptr, 0 };
        session.stream_failure = KIOTTY_NET_CODE_SUCCESS;
        session.is_work_requested = false;
        const BodyFraming no_body{ 0, BodyKind::NONE };
        session.framing = RequestFraming{ no_body, Persistence::KEEP_ALIVE, false };
        session.phase = SessionPhase::READING;
        session.persistence = Persistence::KEEP_ALIVE;
        return readLine(session, HttpState::REQUEST_LINE, wait_milliseconds);
    }

    KiottyNetTransition readLine(HttpSession& session,
                                 HttpState state,
                                 uint32_t timeout_milliseconds)
    {
        const size_t limit = lineLimitOf(session.site);
        const uint32_t timeout = timeout_milliseconds;
        const KiottyNetConnectionAction action = thenReadUntil(LINE_END_SPAN, limit, timeout);
        return Next::moveToNextState(state, action);
    }

    KiottyNetTransition readBody(HttpSession& session, HttpState state)
    {
        const size_t step = nextReadOf(session.body, session.site.receive_capacity);
        const uint32_t timeout = session.site.limits.header_timeout_milliseconds;
        const KiottyNetConnectionAction action = thenRead(step, timeout);
        return Next::moveToNextState(state, action);
    }

    KiottyNetTransition reject(HttpSession& session, KiottyNetHttpStatus status)
    {
        session.response.begin();
        (void)session.response.setStatus(status);
        session.persistence = Persistence::CLOSE;
        session.close_reason = KIOTTY_NET_CODE_PROTOCOL_ERROR;
        return startSending(session);
    }

    KiottyNetTransition dispatch(HttpSession& session)
    {
        session.phase = SessionPhase::FILLING;
        const KiottyNetHttpRequest& request = session.request.request();
        const Span<const char> path{ request.path.data, request.path.length };
        const RouteMatch match = session.site.registry.find(request.method, path);
        const bool is_route = match.kind == MatchKind::ROUTE;
        if (is_route) { return callRoute(session, match.route); }
        HttpUpgradeHandler* const upgrade = session.site.upgrade;
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
        const KiottyNetHttpWorkHandle work{ session.site.owner, session.id.value() };
        route.onWorkStarted(session.site.app_context, request, response, work);
        return Next::moveToNextState(HttpState::WORKING, thenWork());
    }

    KiottyNetTransition sendNext(HttpSession& session, KiottyNetConnectionHandle connection)
    {
        const PieceResult piece = pieceAt(session, session.sent);
        if (!piece) { return closeWith(piece.code); }
        const ResponseFrame frame = session.response.frame();
        const SendStep step = sendStepOf(frame, session.sent, piece.value, session.persistence);
        if (step.kind != SendKind::DONE) { return sendPiece(session, step); }
        const AfterResponse after = afterResponseOf(session.persistence);
        if (after == AfterResponse::CLOSE) { return closeWith(session.close_reason); }
        const uint32_t wait = session.site.limits.keep_alive_timeout_milliseconds;
        return beginExchange(session, connection, wait);
    }

    KiottyNetTransition startSending(HttpSession& session)
    {
        session.phase = SessionPhase::SENDING;
        sealResponse(session);
        session.sent = 0;
        const ResponseFrame frame = session.response.frame();
        const Span<const char> chunk = frame.chunkAt(0, session.site.send_capacity);
        const SendStep step = sendStepOf(frame, 0, chunk, session.persistence);
        return sendPiece(session, step);
    }

    void sealResponse(HttpSession& session)
    {
        const KiottyNetHttpMethod method = session.request.request().method;
        session.response.seal(ResponseSeal{ method, session.persistence });
    }

    KiottyNetTransition closeWith(KiottyNetCode reason)
    {
        return Next::moveToNextState(HttpState::CLOSING, thenClose(reason));
    }

    void startHeaderClock(HttpSession& session)
    {
        const uint32_t timeout = session.site.limits.header_timeout_milliseconds;
        session.header_deadline = timeout == 0 ? 0 : monotonicMilliseconds() + timeout;
    }

    uint32_t headerTimeLeft(const HttpSession& session)
    {
        if (session.header_deadline == 0) { return 0; }
        const uint64_t now = monotonicMilliseconds();
        if (now >= session.header_deadline) { return EXPIRED_MILLISECONDS; }
        const uint64_t left = session.header_deadline - now;
        const uint64_t largest = std::numeric_limits<uint32_t>::max();
        return static_cast<uint32_t>(left < largest ? left : largest);
    }

    namespace
    {
        size_t lineLimitOf(const HttpSite& site)
        {
            const size_t header_line = site.limits.max_header_bytes + LINE_END_SPAN.length;
            return site.receive_capacity < header_line ? site.receive_capacity : header_line;
        }

        KiottyNetHttpResponseHandle responseHandleOf(const HttpSession& session)
        {
            return KiottyNetHttpResponseHandle{ session.site.owner, session.id.value() };
        }

        KiottyNetTransition callRoute(HttpSession& session, const KiottyNetHttpRoute& route)
        {
            session.route = route;
            const KiottyNetHttpRequest& request = session.request.request();
            const KiottyNetAppContext app_context = session.site.app_context;
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
            return RouteOutcome{ is_stream, is_head, session.is_work_requested };
        }

        KiottyNetTransition awaitWork(HttpSession& session)
        {
            session.phase = SessionPhase::WORKING;
            session.is_worker_owned = true;
            return Next::moveToNextState(HttpState::WORKING, thenWork());
        }

        void answerWithoutWork(HttpSession& session)
        {
            session.response.begin();
            (void)session.response.setStatus(SERVER_ERROR);
            session.persistence = Persistence::CLOSE;
        }

        KiottyNetTransition sendPiece(const HttpSession& session, const SendStep& step)
        {
            const KiottyNetSpan data{ step.chunk.data, step.chunk.length };
            const bool is_farewell = step.kind == SendKind::FAREWELL;
            const KiottyNetConnectionAction write = thenWrite(data, 0);
            if (!is_farewell) { return Next::moveToNextState(HttpState::SENDING, write); }
            const KiottyNetConnectionAction farewell = thenCloseWith(data, session.close_reason);
            return Next::moveToNextState(HttpState::CLOSING, farewell);
        }
    }
}
