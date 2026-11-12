#include "domain/usecases/http/kiotty_http_exchange.h"

#include "domain/entity/kiotty_action.h"
#include "core/kiotty_clock.h"
#include "data/repository/http/kiotty_http_site.h"
#include "domain/usecases/http/kiotty_http_response_text.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;

        constexpr char          LINE_END[] = "\r\n";
        constexpr KiottyNetSpan LINE_END_SPAN{ LINE_END, 2 };
        constexpr uint32_t      EXPIRED_MILLISECONDS = 1;
        constexpr int32_t       NOT_FOUND = KIOTTY_NET_HTTP_STATUS_NOT_FOUND;
        constexpr int32_t       NOT_ALLOWED = KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED;
        constexpr int32_t       NO_CONTENT = KIOTTY_NET_HTTP_STATUS_NO_CONTENT;
        constexpr int32_t       SERVER_ERROR = KIOTTY_NET_HTTP_STATUS_INTERNAL_SERVER_ERROR;
        constexpr char          ALLOW_NAME[] = "Allow";

        size_t                      lineLimitOf(const HttpSite& site);
        KiottyNetHttpResponseHandle responseHandleOf(const HttpSession& session);
        KiottyNetTransition callRoute(HttpSession& session, const KiottyNetHttpRoute& route);
        KiottyNetTransition         afterRoute(HttpSession& session);
        KiottyNetTransition         awaitWork(HttpSession& session);
        void answerAutomatically(HttpSession& session, const RouteMatch& match);
        void answerWithAllow(HttpSession& session, int32_t status, uint32_t allowed);
        void answerWithoutWork(HttpSession& session);
        KiottyNetTransition         startSending(HttpSession& session);
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
        session.id.generation = nextSessionGeneration(session.id.generation);
        session.request.begin(connection);
        session.response.begin();
        session.body_remaining = 0;
        session.sent = 0;
        session.close_reason = KIOTTY_NET_CODE_SUCCESS;
        session.stream = KiottyNetConnectionStream{ nullptr, 0 };
        session.stream_failure = KIOTTY_NET_CODE_SUCCESS;
        session.is_work_requested = false;
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
        const uint64_t capacity = session.site.receive_capacity;
        const uint64_t step = session.body_remaining < capacity ? session.body_remaining : capacity;
        const uint32_t timeout = session.site.limits.header_timeout_milliseconds;
        const KiottyNetConnectionAction action = thenRead(static_cast<size_t>(step), timeout);
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
        answerAutomatically(session, match);
        return startSending(session);
    }

    KiottyNetTransition startWork(HttpSession& session, KiottyNetConnectionStream stream)
    {
        session.stream = stream;
        const KiottyNetHttpRoute route = session.route;
        const KiottyNetHttpRequest* request = &session.request.request();
        const KiottyNetHttpResponseHandle response = responseHandleOf(session);
        const KiottyNetHttpWorkHandle work{ session.site.owner, session.id.value() };
        route.onWorkStarted(route.app_context, request, response, work);
        return Next::moveToNextState(HttpState::WORKING, thenWork());
    }

    KiottyNetTransition sendNext(HttpSession& session, KiottyNetConnectionHandle connection)
    {
        const size_t capacity = session.site.send_capacity;
        const Span<const char> chunk = session.response.frame().chunkAt(session.sent, capacity);
        const KiottyNetSpan data{ chunk.data, chunk.length };
        const KiottyNetConnectionAction write = thenWrite(data, 0);
        if (!chunk.empty()) { return Next::moveToNextState(HttpState::SENDING, write); }
        const bool is_kept = session.persistence == Persistence::KEEP_ALIVE;
        if (!is_kept) { return closeWith(session.close_reason); }
        const uint32_t wait = session.site.limits.keep_alive_timeout_milliseconds;
        return beginExchange(session, connection, wait);
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
            route.onRequest(route.app_context, &request, responseHandleOf(session));
            return afterRoute(session);
        }

        KiottyNetTransition afterRoute(HttpSession& session)
        {
            const bool is_stream = session.response.frame().isStream();
            const bool is_head = session.request.request().method == KIOTTY_NET_HTTP_METHOD_HEAD;
            const bool is_head_stream = is_head && is_stream;
            const bool is_working = session.is_work_requested && !is_head_stream;
            if (is_working) { return awaitWork(session); }
            const bool is_orphan_stream = is_stream && !session.is_work_requested;
            if (is_orphan_stream) { answerWithoutWork(session); }
            return startSending(session);
        }

        KiottyNetTransition awaitWork(HttpSession& session)
        {
            session.phase = SessionPhase::WORKING;
            session.is_worker_owned = true;
            return Next::moveToNextState(HttpState::WORKING, thenWork());
        }

        void answerAutomatically(HttpSession& session, const RouteMatch& match)
        {
            switch (match.kind)
            {
            case MatchKind::ROUTE:
                return;
            case MatchKind::NOT_FOUND:
                (void)session.response.setStatus(NOT_FOUND);
                return;
            case MatchKind::NOT_ALLOWED:
                answerWithAllow(session, NOT_ALLOWED, match.allowed);
                return;
            case MatchKind::OPTIONS:
                answerWithAllow(session, NO_CONTENT, match.allowed);
                return;
            }
        }

        void answerWithAllow(HttpSession& session, int32_t status, uint32_t allowed)
        {
            const AllowText allow = allowListOf(allowed);
            (void)session.response.setStatus(status);
            const Span<const char> name{ ALLOW_NAME, sizeof(ALLOW_NAME) - 1 };
            (void)session.response.addHeader(name, allow.span());
        }

        void answerWithoutWork(HttpSession& session)
        {
            session.response.begin();
            (void)session.response.setStatus(SERVER_ERROR);
            session.persistence = Persistence::CLOSE;
        }

        KiottyNetTransition startSending(HttpSession& session)
        {
            session.phase = SessionPhase::SENDING;
            const KiottyNetHttpMethod method = session.request.request().method;
            session.response.seal(ResponseSeal{ method, session.persistence });
            session.sent = 0;
            const size_t capacity = session.site.send_capacity;
            const Span<const char> chunk = session.response.frame().chunkAt(0, capacity);
            const KiottyNetSpan data{ chunk.data, chunk.length };
            return Next::moveToNextState(HttpState::SENDING, thenWrite(data, 0));
        }
    }
}
