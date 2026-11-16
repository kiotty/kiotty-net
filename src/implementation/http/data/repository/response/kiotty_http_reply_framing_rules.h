#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_reply_framing.h"
#include "http/domain/entity/kiotty_http_status_line.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using ReplyFramingResult = Result<ReplyFraming, KiottyNetCode>;

    ReplyFramingResult selectReplyFraming(KiottyNetHttpMethod method,
                                          const StatusLine& status_line,
                                          Span<const KiottyNetHttpHeader> headers);
}
