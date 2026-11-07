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
        return plain + (plain / TLS_RECORD_PLAIN_BYTES + 1) * TLS_RECORD_OVERHEAD_BYTES + TLS_CONTROL_RESERVE_BYTES;
    }

    constexpr size_t whenTls(size_t count)
    {
        return MemoryLimits::TLS ? count : 0;
    }

    static constexpr size_t CIPHER_RECEIVE_BYTES = whenTls(MemoryLimits::RECEIVE_CAPACITY);
    static constexpr size_t CIPHER_SEND_BYTES = whenTls(alignStage(cipherSendBytes(largerOf(MemoryLimits::SEND_CAPACITY, MemoryLimits::PUSH_CAPACITY))));
}
