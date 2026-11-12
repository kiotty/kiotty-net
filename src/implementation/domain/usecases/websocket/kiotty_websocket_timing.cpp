#include "domain/usecases/websocket/kiotty_websocket_timing.h"

#include "core/kiotty_clock.h"
#include "data/repository/http/kiotty_http_site.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint16_t GOING_AWAY = KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY;
        constexpr uint16_t ABNORMAL = KIOTTY_NET_WEB_SOCKET_STATUS_ABNORMAL_CLOSURE;
        constexpr size_t   STATUS_BYTES = 2;
        constexpr uint32_t EXPIRED_MILLISECONDS = 1;

        uint32_t      closeTimeOf(HttpSession& session);
        uint32_t      timeLeft(uint64_t deadline);
        KiottyNetSpan peerReasonOf(const HttpSession& session);
    }

    bool isClosing(const HttpSession& session)
    {
        return session.link.closing.load() != 0;
    }

    uint32_t readTimeoutOf(HttpSession& session)
    {
        if (isClosing(session)) { return closeTimeOf(session); }
        const uint64_t ping_deadline = session.socket.ping_deadline;
        if (ping_deadline != 0) { return timeLeft(ping_deadline); }
        return session.site.limits.websocket_ping_interval_milliseconds;
    }

    uint64_t deadlineAfter(uint32_t milliseconds)
    {
        if (milliseconds == 0) { return 0; }
        return monotonicMilliseconds() + milliseconds;
    }

    KiottyNetWebSocketClose closeOf(const HttpSession& session, KiottyNetCode reason)
    {
        KiottyNetWebSocketClose close{ reason, ABNORMAL, KiottyNetSpan{ nullptr, 0 } };
        if (session.socket.has_peer_close)
        {
            close.status = session.socket.peer_status;
            close.reason = peerReasonOf(session);
            return close;
        }
        const bool is_dropped = reason == KIOTTY_NET_CODE_PEER_CLOSED
                                || reason == KIOTTY_NET_CODE_SOCKET_ERROR;
        if (is_dropped) { return close; }
        const uint16_t sent = session.link.closing.load();
        const bool is_owner_close = reason == KIOTTY_NET_CODE_CLOSED || sent == 0;
        close.status = is_owner_close ? GOING_AWAY : sent;
        return close;
    }

    namespace
    {
        uint32_t closeTimeOf(HttpSession& session)
        {
            const uint32_t timeout = session.site.limits.websocket_close_timeout_milliseconds;
            if (timeout == 0) { return 0; }
            if (session.socket.close_deadline == 0)
            {
                session.socket.close_deadline = deadlineAfter(timeout);
            }
            return timeLeft(session.socket.close_deadline);
        }

        uint32_t timeLeft(uint64_t deadline)
        {
            const uint64_t now = monotonicMilliseconds();
            if (now >= deadline) { return EXPIRED_MILLISECONDS; }
            const uint64_t left = deadline - now;
            const uint64_t largest = std::numeric_limits<uint32_t>::max();
            return static_cast<uint32_t>(left < largest ? left : largest);
        }

        KiottyNetSpan peerReasonOf(const HttpSession& session)
        {
            const size_t length = session.socket.control_length;
            const bool has_reason = length > STATUS_BYTES;
            const size_t reason_length = has_reason ? length - STATUS_BYTES : 0;
            return KiottyNetSpan{ session.socket.control + STATUS_BYTES, reason_length };
        }
    }
}
