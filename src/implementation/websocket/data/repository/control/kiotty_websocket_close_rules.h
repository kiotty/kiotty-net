#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_close.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using ClosePayloadResult = Result<ClosePayload, KiottyNetWebSocketStatus>;
    using CloseTextResult = Result<CloseText, KiottyNetCode>;

    ClosePayloadResult parseClosePayload(Span<const char> payload);
    CloseTextResult    buildClosePayload(uint16_t status, Span<const char> reason);
    bool               isSendableCloseStatus(uint16_t status);
}
