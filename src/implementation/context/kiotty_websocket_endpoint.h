#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"

#include <cstdint>

struct KiottyNetWebSocketEndpoint
{
    KiottyNetWebSocketEndpoint() = default;
    KiottyNetWebSocketEndpoint(const KiottyNetWebSocketEndpoint&) = delete;
    KiottyNetWebSocketEndpoint& operator=(const KiottyNetWebSocketEndpoint&) = delete;
    virtual ~KiottyNetWebSocketEndpoint();

    template <typename T>
    using Span = kiotty::net::implementation::Span<T>;

    virtual KiottyNetWebSocketInfo socketInfo(int64_t id) = 0;
    virtual KiottyNetSpan          upgradeHeader(int64_t id, Span<const char> name) = 0;
    virtual KiottyNetCode          selectProtocol(int64_t id, Span<const char> protocol) = 0;
    virtual KiottyNetCode          sendSocket(int64_t id,
                                              KiottyNetWebSocketOpcode opcode,
                                              KiottyNetSpan data) = 0;
    virtual KiottyNetCode          closeSocket(int64_t id,
                                               uint16_t status,
                                               Span<const char> reason) = 0;
};
