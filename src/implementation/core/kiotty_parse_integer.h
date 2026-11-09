#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using UnsignedResult = Result<uint64_t, KiottyNetCode>;

    UnsignedResult parseUnsigned(Span<const char> text);
    UnsignedResult parseHexadecimal(Span<const char> text);
}
