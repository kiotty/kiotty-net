#include "websocket/data/repository/control/kiotty_websocket_timing.h"

#include "core/kiotty_clock.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"
#include "websocket/domain/entity/kiotty_websocket_close.h"

#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        uint32_t      closeTimeOf(WebSocketHandler& handler, const UpgradeSession& upgrade);
        uint32_t      timeLeft(uint64_t deadline);
        KiottyNetSpan peerReasonOf(WebSocketHandler& handler, const UpgradeSession& upgrade);
    }

    bool isClosing(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        return linkOf(handler, upgrade).closing.load() != 0;
    }

    uint32_t readTimeoutOf(WebSocketHandler& handler, const UpgradeSession& upgrade)
    {
        if (isClosing(handler, upgrade)) { return closeTimeOf(handler, upgrade); }
        const uint64_t ping_deadline = socketOf(handler, upgrade).ping_deadline;
        if (ping_deadline != 0) { return timeLeft(ping_deadline); }
        return upgrade.limits.websocket_ping_interval_milliseconds;
    }

    uint64_t deadlineAfter(uint32_t milliseconds)
    {
        if (milliseconds == 0) { return 0; }
        return monotonicMilliseconds() + milliseconds;
    }

    KiottyNetWebSocketClose closeOf(WebSocketHandler& handler,
                                    const UpgradeSession& upgrade,
                                    KiottyNetCode reason)
    {
        KiottyNetWebSocketClose close{ reason, ABNORMAL, KiottyNetSpan{ nullptr, 0 } };
        const SocketState& socket = socketOf(handler, upgrade);
        if (socket.has_peer_close)
        {
            close.status = socket.peer_status;
            close.reason = peerReasonOf(handler, upgrade);
            return close;
        }
        const bool is_dropped = reason == KIOTTY_NET_CODE_PEER_CLOSED
                                || reason == KIOTTY_NET_CODE_SOCKET_ERROR;
        if (is_dropped) { return close; }
        const uint16_t sent = linkOf(handler, upgrade).closing.load();
        const bool is_owner_close = reason == KIOTTY_NET_CODE_CLOSED || sent == 0;
        close.status = is_owner_close ? GOING_AWAY : sent;
        return close;
    }

    namespace
    {
        uint32_t closeTimeOf(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            const uint32_t timeout = upgrade.limits.websocket_close_timeout_milliseconds;
            if (timeout == 0) { return 0; }
            SocketState& socket = socketOf(handler, upgrade);
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

        KiottyNetSpan peerReasonOf(WebSocketHandler& handler, const UpgradeSession& upgrade)
        {
            const SocketState& socket = socketOf(handler, upgrade);
            const size_t length = socket.control_length;
            const bool has_reason = length > STATUS_BYTES;
            const size_t reason_length = has_reason ? length - STATUS_BYTES : 0;
            return KiottyNetSpan{ socket.control + STATUS_BYTES, reason_length };
        }
    }
}
