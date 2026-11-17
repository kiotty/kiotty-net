#pragma once

#include "core/kiotty_fixed_string.h"
#include "kiotty_net_websocket.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr int32_t NO_SOCKET_PATH = -1;

    enum class SocketRole : uint8_t
    {
        SERVER,
        CLIENT
    };

    struct WebSocketTarget
    {
        KiottyNetWebSocketUpgradeAction on_upgrade;
        KiottyNetWebSocketOpenAction    on_open;
        KiottyNetWebSocketMessageAction on_message;
        KiottyNetWebSocketCloseAction   on_close;
        int32_t                         path;
    };

    struct ClientEvents
    {
        KiottyNetWebSocketClientOpenAction    on_open;
        KiottyNetWebSocketClientMessageAction on_message;
        KiottyNetWebSocketClientCloseAction   on_close;
    };

    using ProtocolText = FixedString<KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH>;
}
