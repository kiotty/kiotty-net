#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

namespace kiotty::net::implementation
{
    KiottyNetCode checkRequestHeader(const KiottyNetHttpHeader& header);
    KiottyNetCode checkRequestPath(KiottyNetHttpMethod method, Span<const char> path);
    KiottyNetCode checkRequestBody(KiottyNetHttpContentType type, Span<const char> body);
}
