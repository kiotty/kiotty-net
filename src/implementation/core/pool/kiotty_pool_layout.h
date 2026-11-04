#if !defined(KIOTTY_POOL_LAYOUT_H)
#define KIOTTY_POOL_LAYOUT_H

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    static const size_t POOL_ALIGNMENT = 16;
    static const size_t POOL_MAX_CLASSES = 32;

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
        static const size_t ARENA_BYTES = BlockBytes * BlockCount;

        static_assert(BlockBytes >= POOL_ALIGNMENT, "a block must hold at least POOL_ALIGNMENT bytes");
        static_assert(BlockBytes % POOL_ALIGNMENT == 0, "block bytes must be a multiple of POOL_ALIGNMENT");
        static_assert(BlockCount <= SIZE_MAX / BlockBytes, "a class must not overflow size_t");
    };

    constexpr size_t sumArenaBytes()
    {
        return 0;
    }

    template <class... Rest>
    constexpr size_t sumArenaBytes(size_t first, Rest... rest)
    {
        return first + sumArenaBytes(rest...);
    }

    constexpr bool arenaFits()
    {
        return true;
    }

    template <class... Rest>
    constexpr bool arenaFits(size_t first, Rest... rest)
    {
        return arenaFits(rest...) && first <= SIZE_MAX - sumArenaBytes(rest...);
    }

    template <class... Classes>
    struct PoolLayout
    {
        static const size_t CLASS_COUNT = sizeof...(Classes);
        static const size_t ARENA_BYTES = sumArenaBytes(Classes::ARENA_BYTES...);

        static_assert(CLASS_COUNT <= POOL_MAX_CLASSES, "a layout holds at most POOL_MAX_CLASSES classes");
        static_assert(arenaFits(Classes::ARENA_BYTES...), "a layout must not overflow size_t");

        static const PoolClass* classes()
        {
            static const PoolClass list[] = { { Classes::BLOCK_BYTES, Classes::BLOCK_COUNT }..., { 0, 0 } };
            return list;
        }
    };

    template <size_t BlockBytes, size_t BlockCount>
    const size_t BlockClass<BlockBytes, BlockCount>::BLOCK_BYTES;

    template <size_t BlockBytes, size_t BlockCount>
    const size_t BlockClass<BlockBytes, BlockCount>::BLOCK_COUNT;

    template <size_t BlockBytes, size_t BlockCount>
    const size_t BlockClass<BlockBytes, BlockCount>::ARENA_BYTES;

    template <class... Classes>
    const size_t PoolLayout<Classes...>::CLASS_COUNT;

    template <class... Classes>
    const size_t PoolLayout<Classes...>::ARENA_BYTES;
}

#endif
