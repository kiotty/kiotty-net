#pragma once

#include "kiotty_net_http.h"

#include <cstddef>
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

    HttpUpload    idleUpload();
    HttpUpload    startUpload(KiottyNetHttpMethod method, uint64_t body_length);
    KiottyNetCode checkUploadPiece(const HttpUpload& upload, size_t length);
    HttpUpload    takeUploadPiece(const HttpUpload& upload, size_t length);
    KiottyNetCode checkUploadEnd(const HttpUpload& upload);
}
