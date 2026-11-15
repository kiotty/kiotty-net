#pragma once

#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr KiottyNetCode INVALID = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    inline constexpr KiottyNetCode PROTOCOL_ERROR = KIOTTY_NET_CODE_PROTOCOL_ERROR;
    inline constexpr size_t        RESULT_MAXIMUM_BYTES = 0x7FFFFFFF;

    inline KiottyNetResult resultOf(size_t bytes, KiottyNetCode code)
    {
        return KiottyNetResult{ static_cast<int32_t>(bytes), code };
    }
}
