#pragma once

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t CHUNK_LINE_BYTES = 256;

    struct BodyStep
    {
        size_t        consumed;
        size_t        produced;
        KiottyNetCode code;
        bool          is_finished;
    };

    static_assert(sizeof(BodyStep) == 2 * sizeof(size_t) + 2 * sizeof(int32_t),
                  "BodyStep: unexpected padding");

    enum class BodyState : uint8_t
    {
        DONE,
        FAILED,
        CUT_SHORT,
        LENGTH,
        UNTIL_CLOSE,
        CHUNK_SIZE,
        CHUNK_DATA,
        CHUNK_DATA_END,
        CHUNK_DATA_LINE_FEED,
        TRAILER_LINE_START,
        TRAILER_LINE_REST
    };
}
