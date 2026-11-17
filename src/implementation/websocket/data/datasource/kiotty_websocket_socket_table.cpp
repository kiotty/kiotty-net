#include "websocket/data/datasource/kiotty_websocket_socket_table.h"

namespace kiotty::net::implementation
{
    SocketTable::SocketTable()
        : _sockets()
        , _links()
        , _count(0)
    {
    }

    void SocketTable::initialize(size_t count)
    {
        if (_sockets.size() < count) { _sockets = std::vector<SocketState>(count); }
        if (_links.size() < count) { _links = std::vector<SocketLink>(count); }
        for (SocketState& socket : _sockets)
        {
            socket.reset();
        }
        _count = count;
    }

    SocketState& SocketTable::socketAt(int32_t index)
    {
        return _sockets[static_cast<size_t>(index)];
    }

    SocketLink& SocketTable::linkAt(int32_t index)
    {
        return _links[static_cast<size_t>(index)];
    }

    Span<SocketLink> SocketTable::links()
    {
        return Span<SocketLink>{ _links.data(), _count };
    }
}
