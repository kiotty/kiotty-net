#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    struct PoolHandle
    {
        int32_t generation;
        int32_t index;

        int64_t value() const;

        static PoolHandle fromValue(int64_t value);
    };

    static_assert(sizeof(PoolHandle) == 8, "PoolHandle packs into one int64");

    inline int64_t PoolHandle::value() const
    {
        const uint64_t high = static_cast<uint32_t>(generation);
        const uint64_t low = static_cast<uint32_t>(index);
        const uint64_t bits = (high << 32) | low;
        return static_cast<int64_t>(bits);
    }

    inline PoolHandle PoolHandle::fromValue(int64_t value)
    {
        const uint64_t bits = static_cast<uint64_t>(value);
        const uint32_t high = static_cast<uint32_t>(bits >> 32);
        const uint32_t low = static_cast<uint32_t>(bits);
        return PoolHandle{ static_cast<int32_t>(high), static_cast<int32_t>(low) };
    }
}
