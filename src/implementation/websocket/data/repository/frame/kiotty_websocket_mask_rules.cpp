#include "websocket/data/repository/frame/kiotty_websocket_mask_rules.h"

#include "core/kiotty_bytes.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t MIX_FIRST = 0xBF58476D1CE4E5B9ULL;
        constexpr uint64_t MIX_SECOND = 0x94D049BB133111EBULL;
    }

    void applyMask(Span<char> data, const MaskKey& key, size_t offset)
    {
        for (size_t i = 0; i < data.length; ++i)
        {
            const uint8_t mask = key.bytes[(offset + i) % MASK_KEY_BYTES];
            data[i] = static_cast<char>(static_cast<uint8_t>(data[i]) ^ mask);
        }
    }

    MaskKey nextMaskKey(std::atomic<uint64_t>& state)
    {
        const uint64_t drawn = state.fetch_add(GOLDEN_GAMMA) + GOLDEN_GAMMA;
        uint64_t mixed = (drawn ^ (drawn >> 30U)) * MIX_FIRST;
        mixed = (mixed ^ (mixed >> 27U)) * MIX_SECOND;
        mixed = mixed ^ (mixed >> 31U);
        MaskKey key{};
        for (size_t i = 0; i < MASK_KEY_BYTES; ++i)
        {
            key.bytes[i] = static_cast<uint8_t>(mixed >> (BITS_PER_BYTE * i));
        }
        return key;
    }
}
