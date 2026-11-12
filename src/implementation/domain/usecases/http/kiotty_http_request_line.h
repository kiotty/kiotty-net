#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class HttpVersion : uint8_t
    {
        V10,
        V11
    };

    struct RequestLine
    {
        Span<const char>    path;
        Span<const char>    query;
        KiottyNetHttpMethod method;
        HttpVersion         version;
    };

    using RequestLineResult = Result<RequestLine, KiottyNetHttpStatus>;

    RequestLineResult parseRequestLine(Span<const char> line);
}
