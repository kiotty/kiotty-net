#pragma once

#include "data/datasource/kiotty_memory_limits.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    static constexpr size_t TLS_RECORD_PLAIN_BYTES = 16384;
    static constexpr size_t TLS_RECORD_OVERHEAD_BYTES = 64;
    static constexpr size_t TLS_CONTROL_RESERVE_BYTES = 512;
    static constexpr size_t STAGE_ALIGNMENT = 16;

    constexpr size_t alignStage(size_t bytes)
    {
        return (bytes + STAGE_ALIGNMENT - 1) / STAGE_ALIGNMENT * STAGE_ALIGNMENT;
    }

    constexpr size_t largerOf(size_t left, size_t right)
    {
        return left > right ? left : right;
    }

    constexpr size_t cipherSendBytes(size_t plain)
    {
        const size_t records = plain / TLS_RECORD_PLAIN_BYTES + 1;
        return plain + records * TLS_RECORD_OVERHEAD_BYTES + TLS_CONTROL_RESERVE_BYTES;
    }
}
