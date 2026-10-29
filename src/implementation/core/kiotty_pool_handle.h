#if !defined(KIOTTY_POOL_HANDLE_H)
#define KIOTTY_POOL_HANDLE_H

#include <cstdint>

namespace kiotty
{
    struct PoolHandle
    {
        int32_t generation;
        int32_t index;
    };

    struct OwnerIndex
    {
        uint32_t context_slot;
        uint32_t owner_slot;
        uint32_t owner_generation;
    };

    static const uint32_t CONTEXT_SLOT_BITS     = 4;
    static const uint32_t OWNER_SLOT_BITS       = 4;
    static const uint32_t OWNER_GENERATION_BITS = 8;
    static const uint32_t CONNECTION_SLOT_BITS  = 16;

    static const uint32_t CONTEXT_SLOT_MASK     = (1u << CONTEXT_SLOT_BITS) - 1u;
    static const uint32_t OWNER_SLOT_MASK       = (1u << OWNER_SLOT_BITS) - 1u;
    static const uint32_t OWNER_GENERATION_MASK = (1u << OWNER_GENERATION_BITS) - 1u;
    static const uint32_t CONNECTION_SLOT_MASK  = (1u << CONNECTION_SLOT_BITS) - 1u;

    static const uint32_t OWNER_GENERATION_SHIFT = CONNECTION_SLOT_BITS;
    static const uint32_t OWNER_SLOT_SHIFT       = OWNER_GENERATION_SHIFT + OWNER_GENERATION_BITS;
    static const uint32_t CONTEXT_SLOT_SHIFT     = OWNER_SLOT_SHIFT + OWNER_SLOT_BITS;

    inline int64_t toRaw(PoolHandle handle)
    {
        uint64_t high = static_cast<uint32_t>(handle.generation);
        uint64_t low = static_cast<uint32_t>(handle.index);
        uint64_t raw = (high << 32) | low;
        return static_cast<int64_t>(raw);
    }

    inline PoolHandle fromRaw(int64_t raw)
    {
        uint64_t bits = static_cast<uint64_t>(raw);
        uint32_t high = static_cast<uint32_t>(bits >> 32);
        uint32_t low = static_cast<uint32_t>(bits);
        PoolHandle handle;
        handle.generation = static_cast<int32_t>(high);
        handle.index = static_cast<int32_t>(low);
        return handle;
    }

    inline int32_t makeIndex(OwnerIndex owner, uint32_t slot)
    {
        uint32_t context_bits = (owner.context_slot & CONTEXT_SLOT_MASK) << CONTEXT_SLOT_SHIFT;
        uint32_t owner_bits = (owner.owner_slot & OWNER_SLOT_MASK) << OWNER_SLOT_SHIFT;
        uint32_t generation_bits = (owner.owner_generation & OWNER_GENERATION_MASK) << OWNER_GENERATION_SHIFT;
        uint32_t slot_bits = slot & CONNECTION_SLOT_MASK;
        uint32_t index = context_bits | owner_bits | generation_bits | slot_bits;
        return static_cast<int32_t>(index);
    }

    inline OwnerIndex ownerOf(int32_t index)
    {
        uint32_t bits = static_cast<uint32_t>(index);
        OwnerIndex owner;
        owner.context_slot = (bits >> CONTEXT_SLOT_SHIFT) & CONTEXT_SLOT_MASK;
        owner.owner_slot = (bits >> OWNER_SLOT_SHIFT) & OWNER_SLOT_MASK;
        owner.owner_generation = (bits >> OWNER_GENERATION_SHIFT) & OWNER_GENERATION_MASK;
        return owner;
    }

    inline uint32_t slotOf(int32_t index)
    {
        uint32_t bits = static_cast<uint32_t>(index);
        return bits & CONNECTION_SLOT_MASK;
    }
}

#endif
