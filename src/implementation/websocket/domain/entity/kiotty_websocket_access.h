#pragma once

#include "core/kiotty_span.h"
#include "http/data/datasource/kiotty_http_session_pool.h"
#include "http/data/repository/kiotty_http_site.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/kiotty_websocket_handler.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class WebSocketAccess
    {
    public:
        explicit WebSocketAccess(HttpSite& site);

        KiottyNetWebSocketInfo info(int64_t id);
        KiottyNetSpan          header(int64_t id, Span<const char> name);
        KiottyNetCode          select(int64_t id, Span<const char> protocol);
        KiottyNetCode          send(int64_t id,
                                    KiottyNetWebSocketOpcode opcode,
                                    KiottyNetSpan data);
        KiottyNetCode          close(int64_t id, uint16_t status, Span<const char> reason);
        KiottyNetCode          broadcast(const char* path,
                                         KiottyNetWebSocketOpcode opcode,
                                         KiottyNetSpan data);

    private:
        using LinkResult = Result<SocketLink&, KiottyNetCode>;

        SessionResult upgrading(int64_t id);
        LinkResult    openLink(int64_t id);

        HttpSite&         _site;
        WebSocketHandler& _handler;
    };
}
