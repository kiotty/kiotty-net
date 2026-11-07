#pragma once

#include "core/kiotty_span.h"
#include "data/infra/socket/kiotty_active_socket.h"
#include "domain/entity/kiotty_socket_code.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class NoDelay : uint8_t
    {
        DISABLED,
        ENABLED
    };

    TransferResult send(const ActiveSocket& socket, Span<const char> data);
    TransferResult receive(const ActiveSocket& socket, Span<char> buffer);
    TransferResult peek(const ActiveSocket& socket, Span<char> buffer);
    SocketCode     waitReadable(const ActiveSocket& socket, uint32_t timeout_milliseconds);
    SocketCode     waitWritable(const ActiveSocket& socket, uint32_t timeout_milliseconds);
    SocketCode     shutdownWrite(const ActiveSocket& socket);
    SocketCode     configureNoDelay(const ActiveSocket& socket, NoDelay no_delay);
}
