#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    KiottyNetSpan findReplyHeader(Span<const KiottyNetHttpHeader> headers, Span<const char> name);
    uint32_t      allowedMethodsOf(Span<const KiottyNetHttpHeader> headers);
}
