#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr uint16_t HTTP_DEFAULT_PORT = 80;
    inline constexpr uint16_t HTTPS_DEFAULT_PORT = 443;
    inline constexpr size_t   HOST_BRACKET_BYTES = 2;
    inline constexpr size_t   HOST_PORT_BYTES = 6;
    inline constexpr size_t   HOST_FIELD_BYTES =
        KIOTTY_NET_MAX_HOST_LENGTH + HOST_BRACKET_BYTES + HOST_PORT_BYTES;

    using HostField = FixedString<HOST_FIELD_BYTES + 1>;

    struct RequestHead
    {
        Span<const KiottyNetHttpHeader> shared_headers;
        Span<const KiottyNetHttpHeader> extra_headers;
        Span<const char>                path;
        Span<const char>                host_field;
        uint64_t                        body_length;
        size_t                          max_headers;
        KiottyNetHttpMethod             method;
        KiottyNetHttpContentType        type;
    };

    static_assert(sizeof(RequestHead) == 4 * sizeof(Span<const char>) + sizeof(uint64_t)
                                             + sizeof(size_t) + 2 * sizeof(int32_t),
                  "RequestHead: unexpected padding");
}
