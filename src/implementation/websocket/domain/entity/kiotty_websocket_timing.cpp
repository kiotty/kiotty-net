#include "websocket/domain/entity/kiotty_websocket_timing.h"

#include "core/kiotty_clock.h"
#include "websocket/data/repository/kiotty_websocket_handler.h"
#include "websocket/domain/entity/kiotty_websocket_close.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        uint32_t      closeTimeOf(HttpSession& session);
        uint32_t      timeLeft(uint64_t deadline);
        KiottyNetSpan peerReasonOf(const HttpSession& session);
    }

    bool isClosing(const HttpSession& session)
    {
        return linkOf(session).closing.load() != 0;
    }

    uint32_t readTimeoutOf(HttpSession& session)
    {
        if (isClosing(session)) { return closeTimeOf(session); }
        const uint64_t ping_deadline = socketOf(session).ping_deadline;
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
        const SocketState& socket = socketOf(session);
        if (socket.has_peer_close)
        {
            close.status = socket.peer_status;
            close.reason = peerReasonOf(session);
            return close;
        }
        const bool is_dropped = reason == KIOTTY_NET_CODE_PEER_CLOSED
                                || reason == KIOTTY_NET_CODE_SOCKET_ERROR;
        if (is_dropped) { return close; }
        const uint16_t sent = linkOf(session).closing.load();
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
            SocketState& socket = socketOf(session);
            if (socket.close_deadline == 0) { socket.close_deadline = deadlineAfter(timeout); }
            return timeLeft(socket.close_deadline);
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
            const SocketState& socket = socketOf(session);
            const size_t length = socket.control_length;
            const bool has_reason = length > STATUS_BYTES;
            const size_t reason_length = has_reason ? length - STATUS_BYTES : 0;
            return KiottyNetSpan{ socket.control + STATUS_BYTES, reason_length };
        }
    }
}
