#pragma once

#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr uint64_t CHUNKED_BODY_LENGTH = KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN;

    enum class UploadMode : int32_t
    {
        IDLE,
        LENGTH,
        CHUNKED,
    };

    struct HttpUpload
    {
        uint64_t            left;
        KiottyNetHttpMethod method;
        UploadMode          mode;
    };

    static_assert(sizeof(HttpUpload) == sizeof(uint64_t) + 2 * sizeof(int32_t),
                  "HttpUpload: unexpected padding");
}
