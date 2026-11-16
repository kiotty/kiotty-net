#pragma once

#include "core/kiotty_fixed_string.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    inline constexpr size_t RESPONSE_PREFIX_BYTES = 256;
    inline constexpr size_t ALLOW_TEXT_BYTES = 64;
    inline constexpr size_t CHUNK_HEAD_BYTES = 18;
    inline constexpr size_t DECIMAL_DIGITS = 20;

    using PrefixText = FixedString<RESPONSE_PREFIX_BYTES + 1>;
    using AllowText = FixedString<ALLOW_TEXT_BYTES>;
    using ChunkHead = FixedString<CHUNK_HEAD_BYTES + 1>;
    using DecimalText = FixedString<DECIMAL_DIGITS + 1>;
}
