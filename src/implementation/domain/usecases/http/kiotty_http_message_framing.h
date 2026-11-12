#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "domain/usecases/http/kiotty_http_request_line.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class BodyKind : uint8_t
    {
        NONE,
        LENGTH,
        CHUNKED
    };

    enum class Persistence : uint8_t
    {
        CLOSE,
        KEEP_ALIVE
    };

    struct BodyFraming
    {
        uint64_t length;
        BodyKind kind;
    };

    using LengthResult = Result<uint64_t, KiottyNetHttpStatus>;
    using FramingResult = Result<BodyFraming, KiottyNetHttpStatus>;

    LengthResult  parseContentLength(Span<const char> value);
    LengthResult  parseChunkSize(Span<const char> line);
    FramingResult selectBodyFraming(Span<const KiottyNetHttpHeader> headers);
    Persistence   decidePersistence(HttpVersion version, Span<const KiottyNetHttpHeader> headers);
    bool          expectsContinue(HttpVersion version, Span<const KiottyNetHttpHeader> headers);
}
