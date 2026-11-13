#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_parker.h"
#include "core/kiotty_span.h"
#include "domain/usecases/websocket/kiotty_websocket_frame.h"
#include "kiotty_net_websocket.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr int32_t NO_SOCKET_PATH = -1;
    inline constexpr size_t  CLOSE_FRAME_BYTES =
        FRAME_START_BYTES + MASK_KEY_BYTES + MAX_CONTROL_PAYLOAD_BYTES;

    enum class SocketRole : uint8_t
    {
        SERVER,
        CLIENT
    };

    struct WebSocketTarget
    {
        KiottyNetWebSocketHandler handler;
        KiottyNetAppContext       app_context;
        int32_t                   path;
    };

    struct SocketLink
    {
        SocketLink();

        std::atomic<int64_t>  socket;
        std::atomic<int64_t>  connection;
        std::atomic<int32_t>  path;
        std::atomic<uint16_t> closing;
    };

    struct SocketFrame
    {
        uint64_t      remaining;
        size_t        offset;
        MaskKey       key;
        FrameOpcode   opcode;
        FrameFinality finality;
        FrameMasking  masking;
    };

    struct SocketRead
    {
        size_t  length;
        int32_t state;
    };

    struct ClientUpgrade
    {
        WebSocketTarget  target;
        Span<const char> request;
        Span<const char> accept;
        Parker*          opened;
        KiottyNetCode    failure;
    };

    using ProtocolText = FixedString<KIOTTY_NET_MAX_WEB_SOCKET_PROTOCOL_LENGTH>;

    struct SocketState
    {
        SocketState();

        void reset();

        WebSocketTarget           target;
        KiottyNetWebSocketContext context;
        ProtocolText              protocol;
        SocketFrame               frame;
        SocketRead                read;
        uint64_t                  ping_deadline;
        uint64_t                  close_deadline;
        size_t                    control_length;
        uint16_t                  peer_status;
        FrameOpcode               message;
        char                      control[MAX_CONTROL_PAYLOAD_BYTES];
        char                      outgoing[CLOSE_FRAME_BYTES];
        bool                      is_open;
        bool                      has_peer_close;
    };

    class PushLane
    {
    public:
        PushLane() = default;
        PushLane(const PushLane&) = delete;
        PushLane& operator=(const PushLane&) = delete;

        virtual KiottyNetCode push(int64_t connection,
                                   FrameOpcode opcode,
                                   Span<const char> payload) = 0;

    protected:
        ~PushLane() = default;
    };
}
