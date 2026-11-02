#if !defined(KIOTTY_POOL_LAYOUT_H)
#define KIOTTY_POOL_LAYOUT_H

#include <cstddef>

namespace kiotty
{
    static const size_t POOL_ALIGNMENT = 16;

    constexpr size_t alignBlock(size_t bytes)
    {
        return (bytes + POOL_ALIGNMENT - 1) / POOL_ALIGNMENT * POOL_ALIGNMENT;
    }

    struct PoolClass
    {
        size_t block_bytes;
        size_t block_count;
    };

    template <size_t BlockBytes, size_t BlockCount>
    struct BlockClass
    {
        static const size_t BLOCK_BYTES = BlockBytes;
        static const size_t BLOCK_COUNT = BlockCount;

        static_assert(BlockBytes >= POOL_ALIGNMENT, "a block must hold at least POOL_ALIGNMENT bytes");
        static_assert(BlockBytes % POOL_ALIGNMENT == 0, "block bytes must be a multiple of POOL_ALIGNMENT");
    };

    template <class... Classes>
    struct PoolLayout
    {
        static const size_t CLASS_COUNT = sizeof...(Classes);

        static const PoolClass* classes()
        {
            static const PoolClass list[] = { { Classes::BLOCK_BYTES, Classes::BLOCK_COUNT }... };
            return list;
        }
    };
}

#endif
