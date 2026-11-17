#pragma once

#include "core/kiotty_span.h"
#include "websocket/domain/entity/kiotty_websocket_handshake.h"

namespace kiotty::net::implementation
{
    bool       isWebSocketKey(Span<const char> key);
    AcceptText acceptOf(Span<const char> key);
}
