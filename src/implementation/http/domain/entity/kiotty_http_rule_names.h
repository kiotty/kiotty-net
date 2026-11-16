#pragma once

#include "http/domain/entity/kiotty_http_exchange.h"
#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    inline constexpr HttpState HEADER_LINE = HttpState::HEADER_LINE;
    inline constexpr HttpState CLOSING = HttpState::CLOSING;
    inline constexpr HttpState FRAME_START = HttpState::FRAME_START;
    inline constexpr HttpState FRAME_LENGTH = HttpState::FRAME_LENGTH;
    inline constexpr HttpState FRAME_MASK = HttpState::FRAME_MASK;
    inline constexpr HttpState FRAME_PAYLOAD = HttpState::FRAME_PAYLOAD;

    inline constexpr KiottyNetConnectionEventType CONNECTED =
        KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED;
    inline constexpr KiottyNetConnectionEventType READ = KIOTTY_NET_CONNECTION_EVENT_TYPE_READ;
    inline constexpr KiottyNetConnectionEventType WRITTEN =
        KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN;
    inline constexpr KiottyNetConnectionEventType TIMEOUT =
        KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT;
    inline constexpr KiottyNetConnectionEventType CLOSED = KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED;
}
