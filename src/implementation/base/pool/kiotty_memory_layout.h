#if !defined(KIOTTY_MEMORY_LAYOUT_H)
#define KIOTTY_MEMORY_LAYOUT_H

#include "data/datasource/kiotty_memory_limits.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    typedef MemoryLimits Limits;

    static const size_t TLS_RECORD_PLAIN_BYTES = 16384;
    static const size_t TLS_RECORD_OVERHEAD_BYTES = 64;
    static const size_t TLS_CONTROL_RESERVE_BYTES = 512;
    static const size_t STAGE_ALIGNMENT = 16;

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
        return Limits::TLS ? count : 0;
    }

    static const size_t CIPHER_RECEIVE_BYTES = whenTls(Limits::RECEIVE_CAPACITY);
    static const size_t CIPHER_SEND_BYTES = whenTls(alignStage(cipherSendBytes(largerOf(Limits::SEND_CAPACITY, Limits::PUSH_CAPACITY))));
}

#endif
