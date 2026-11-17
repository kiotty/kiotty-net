#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/state/kiotty_websocket_socket_state.h"
#include "websocket/domain/entity/kiotty_websocket_frame.h"

#include <cstdint>

namespace kiotty::net::implementation
{
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

    KiottyNetCode pushFrame(PushLane& lane,
                            const SocketLink& link,
                            FrameOpcode opcode,
                            Span<const char> payload);
    KiottyNetCode pushClose(PushLane& lane,
                            SocketLink& link,
                            uint16_t status,
                            Span<const char> reason);
    bool          isOpenSocket(const SocketLink& link, int64_t socket);
}
