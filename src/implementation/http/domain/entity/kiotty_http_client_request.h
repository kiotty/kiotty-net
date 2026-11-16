#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct ClientRequest
    {
        Span<const KiottyNetHttpHeader> headers;
        Span<const char>                path;
        Span<const char>                body;
        KiottyNetHttpMethod             method;
        KiottyNetHttpContentType        type;
    };

    static_assert(sizeof(ClientRequest) == 3 * sizeof(Span<const char>) + 2 * sizeof(int32_t),
                  "ClientRequest: unexpected padding");
}
