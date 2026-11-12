#pragma once

#include "core/kiotty_span.h"
#include "data/repository/websocket/kiotty_websocket_socket.h"
#include "domain/usecases/websocket/kiotty_websocket_frame.h"
#include "kiotty_net_websocket.h"

#include <cstdint>

namespace kiotty::net::implementation
{
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
