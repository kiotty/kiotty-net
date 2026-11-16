#pragma once

#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr KiottyNetHttpStatus BAD_REQUEST = KIOTTY_NET_HTTP_STATUS_BAD_REQUEST;
    inline constexpr KiottyNetHttpStatus NOT_IMPLEMENTED = KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED;
    inline constexpr KiottyNetHttpStatus TOO_LARGE = KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE;
    inline constexpr KiottyNetHttpStatus CONTENT_TOO_LARGE =
        KIOTTY_NET_HTTP_STATUS_CONTENT_TOO_LARGE;
    inline constexpr int32_t NOT_FOUND = KIOTTY_NET_HTTP_STATUS_NOT_FOUND;
    inline constexpr char    CONTENT_LENGTH[] = "Content-Length";
    inline constexpr char    TRANSFER_ENCODING[] = "Transfer-Encoding";
}
