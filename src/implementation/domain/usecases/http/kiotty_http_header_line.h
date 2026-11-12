#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    using HeaderResult = Result<KiottyNetHttpHeader, KiottyNetHttpStatus>;

    HeaderResult parseHeaderLine(Span<const char> line, size_t max_length);
}
