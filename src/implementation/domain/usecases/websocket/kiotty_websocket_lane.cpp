#include "domain/usecases/websocket/kiotty_websocket_lane.h"

#include "domain/usecases/websocket/kiotty_websocket_close.h"

namespace kiotty::net::implementation
{
    KiottyNetCode pushFrame(PushLane& lane,
                            const SocketLink& link,
                            FrameOpcode opcode,
                            Span<const char> payload)
    {
        return lane.push(link.connection.load(), opcode, payload);
    }

    KiottyNetCode pushClose(PushLane& lane,
                            SocketLink& link,
                            uint16_t status,
                            Span<const char> reason)
    {
        const CloseTextResult payload = buildClosePayload(status, reason);
        if (!payload) { return payload.code; }
        uint16_t open = 0;
        const bool is_first = link.closing.compare_exchange_strong(open, status);
        if (!is_first) { return KIOTTY_NET_CODE_BUSY; }
        return pushFrame(lane, link, FrameOpcode::CLOSE, payload.value.span());
    }

    bool isOpenSocket(const SocketLink& link, int64_t socket)
    {
        return socket != 0 && link.socket.load() == socket;
    }
}
