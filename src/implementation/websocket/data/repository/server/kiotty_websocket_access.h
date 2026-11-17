#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/repository/server/kiotty_http_registry.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class WebSocketAccess
    {
    public:
        WebSocketAccess(const HttpRegistry& registry,
                        WebSocketHandler& handler,
                        size_t push_capacity);

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
        using OfferResult = Result<const UpgradeSession&, KiottyNetCode>;

        OfferResult   upgrading(int64_t id);
        LinkResult    openLink(int64_t id);
        KiottyNetCode checkData(KiottyNetWebSocketOpcode opcode, KiottyNetSpan data) const;

        const HttpRegistry& _registry;
        WebSocketHandler&   _handler;
        size_t              _push_capacity;
    };
}
