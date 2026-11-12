#pragma once

#include "core/kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t SHA1_DIGEST_BYTES = 20;

    struct Sha1Digest
    {
        uint8_t bytes[SHA1_DIGEST_BYTES];
    };

    Sha1Digest sha1Of(Span<const char> first, Span<const char> second);
}
