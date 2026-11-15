#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "domain/usecases/http/kiotty_http_message_framing.h"
#include "domain/usecases/http/kiotty_http_status_line.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class ReplyBodyKind : uint8_t
    {
        NONE,
        LENGTH,
        CHUNKED,
        UNTIL_CLOSE
    };

    enum class ReplyPhase : uint8_t
    {
        FINAL,
        INTERIM
    };

    struct ReplyFraming
    {
        uint64_t      length;
        ReplyBodyKind kind;
        Persistence   persistence;
        ReplyPhase    phase;
    };

    static_assert(sizeof(ReplyFraming) == 2 * sizeof(uint64_t), "ReplyFraming: unexpected padding");

    using ReplyFramingResult = Result<ReplyFraming, KiottyNetCode>;

    ReplyFramingResult selectReplyFraming(KiottyNetHttpMethod method,
                                          const StatusLine& status_line,
                                          Span<const KiottyNetHttpHeader> headers);
}
