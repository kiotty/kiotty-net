#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "domain/usecases/websocket/kiotty_websocket_frame.h"
#include "kiotty_net_websocket.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t   STATUS_BYTES = 2;
    inline constexpr uint16_t NO_STATUS = KIOTTY_NET_WEB_SOCKET_STATUS_NO_STATUS;
    inline constexpr uint16_t ABNORMAL = KIOTTY_NET_WEB_SOCKET_STATUS_ABNORMAL_CLOSURE;
    inline constexpr uint16_t GOING_AWAY = KIOTTY_NET_WEB_SOCKET_STATUS_GOING_AWAY;

    inline constexpr KiottyNetWebSocketStatus CLOSE_INVALID_PAYLOAD =
        KIOTTY_NET_WEB_SOCKET_STATUS_INVALID_PAYLOAD;

    struct ClosePayload
    {
        Span<const char> reason;
        uint16_t         status;
    };

    using ClosePayloadResult = Result<ClosePayload, KiottyNetWebSocketStatus>;
    using CloseText = FixedString<MAX_CONTROL_PAYLOAD_BYTES + 1>;
    using CloseTextResult = Result<CloseText, KiottyNetCode>;

    ClosePayloadResult parseClosePayload(Span<const char> payload);
    CloseTextResult    buildClosePayload(uint16_t status, Span<const char> reason);
    bool               isSendableCloseStatus(uint16_t status);
}
