#if !defined(KIOTTY_POOL_HANDLE_H)
#define KIOTTY_POOL_HANDLE_H

#include <cstdint>

namespace kiotty
{
    struct PoolHandle
    {
        int32_t generation;
        int32_t index;

        int64_t value() const
        {
            uint64_t high = static_cast<uint32_t>(generation);
            uint64_t low = static_cast<uint32_t>(index);
            uint64_t bits = (high << 32) | low;
            return static_cast<int64_t>(bits);
        }

        static PoolHandle fromValue(int64_t value)
        {
            uint64_t bits = static_cast<uint64_t>(value);
            uint32_t high = static_cast<uint32_t>(bits >> 32);
            uint32_t low = static_cast<uint32_t>(bits);
            PoolHandle handle;
            handle.generation = static_cast<int32_t>(high);
            handle.index = static_cast<int32_t>(low);
            return handle;
        }
    };
}

#endif
