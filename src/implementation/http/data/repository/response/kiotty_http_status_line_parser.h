#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_status_line.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using StatusLineResult = Result<StatusLine, KiottyNetCode>;

    StatusLineResult parseStatusLine(Span<const char> line);
}
