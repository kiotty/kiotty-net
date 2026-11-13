#include "data/repository/websocket/kiotty_websocket_socket.h"

namespace kiotty::net::implementation
{
    SocketLink::SocketLink()
        : socket(0)
        , connection(0)
        , path(NO_SOCKET_PATH)
        , closing(0)
    {
    }

    SocketState::SocketState()
        : target{}
        , context(nullptr)
        , protocol()
        , frame{}
        , read{}
        , ping_deadline(0)
        , close_deadline(0)
        , control_length(0)
        , peer_status(0)
        , message(FrameOpcode::CONTINUATION)
        , control{}
        , outgoing{}
        , is_open(false)
        , has_peer_close(false)
    {
    }

    void SocketState::reset()
    {
        target = WebSocketTarget{ KiottyNetWebSocketHandler{}, nullptr, NO_SOCKET_PATH };
        context = nullptr;
        protocol.clear();
        frame = SocketFrame{};
        read = SocketRead{};
        ping_deadline = 0;
        close_deadline = 0;
        control_length = 0;
        peer_status = 0;
        message = FrameOpcode::CONTINUATION;
        is_open = false;
        has_peer_close = false;
    }
}
