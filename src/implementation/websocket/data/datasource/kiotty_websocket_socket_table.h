#pragma once

#include "core/kiotty_span.h"
#include "websocket/data/state/kiotty_websocket_socket_state.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    class SocketTable
    {
    public:
        SocketTable();

        void             initialize(size_t count);
        SocketState&     socketAt(int32_t index);
        SocketLink&      linkAt(int32_t index);
        Span<SocketLink> links();

    private:
        std::vector<SocketState> _sockets;
        std::vector<SocketLink>  _links;
        size_t                   _count;
    };
}
