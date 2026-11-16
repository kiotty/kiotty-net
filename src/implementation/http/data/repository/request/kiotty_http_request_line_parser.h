#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_request_line.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using RequestLineResult = Result<RequestLine, KiottyNetHttpStatus>;
    using HttpVersionResult = Result<HttpVersion, KiottyNetCode>;

    RequestLineResult parseRequestLine(Span<const char> line);
    HttpVersionResult parseHttpVersion(Span<const char> text);
    Span<const char>  methodTextOf(KiottyNetHttpMethod method);
}
