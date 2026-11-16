#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_message_framing.h"
#include "http/domain/entity/kiotty_http_request_line.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using LengthResult = Result<uint64_t, KiottyNetHttpStatus>;
    using FramingResult = Result<BodyFraming, KiottyNetHttpStatus>;

    LengthResult  parseContentLength(Span<const char> value);
    LengthResult  parseChunkSize(Span<const char> line);
    FramingResult selectBodyFraming(Span<const KiottyNetHttpHeader> headers);
    Persistence   decidePersistence(HttpVersion version, Span<const KiottyNetHttpHeader> headers);
    bool          expectsContinue(HttpVersion version, Span<const KiottyNetHttpHeader> headers);
    bool          hasBody(const BodyFraming& framing);
}
