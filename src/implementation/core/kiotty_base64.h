#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    using EncodedResult = Result<size_t, KiottyNetCode>;

    size_t        base64LengthOf(size_t length);
    EncodedResult encodeBase64(Span<const uint8_t> input, Span<char> target);
    bool          isBase64Text(Span<const char> text);
}
