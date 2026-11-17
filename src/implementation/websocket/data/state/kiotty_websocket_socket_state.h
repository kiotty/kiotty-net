#pragma once

#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"
#include "websocket/domain/entity/kiotty_websocket_frame_step.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct UpgradeSession;

    inline constexpr size_t CLOSE_FRAME_BYTES =
        FRAME_START_BYTES + MASK_KEY_BYTES + MAX_CONTROL_PAYLOAD_BYTES;

    struct SocketLink
    {
        SocketLink();

        std::atomic<int64_t>  socket;
        std::atomic<int64_t>  connection;
        std::atomic<int32_t>  path;
        std::atomic<uint16_t> closing;
    };

    struct SocketState
    {
        SocketState();

        void reset();

        WebSocketTarget           target;
        KiottyNetWebSocketContext context;
        KiottyNetConnectionInfo   connection;
        const UpgradeSession*     upgrading;
        ProtocolText              protocol;
        SocketFrame               frame;
        FrameRead                 read;
        uint64_t                  ping_deadline;
        uint64_t                  close_deadline;
        size_t                    control_length;
        size_t                    message_length;
        uint16_t                  peer_status;
        FrameOpcode               message;
        char                      control[MAX_CONTROL_PAYLOAD_BYTES];
        char                      outgoing[CLOSE_FRAME_BYTES];
        bool                      is_open;
        bool                      has_peer_close;
    };
}
