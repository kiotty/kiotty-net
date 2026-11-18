#pragma once

#include "http/domain/entity/kiotty_http_upload.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    HttpUpload    idleUpload();
    HttpUpload    startUpload(KiottyNetHttpMethod method, uint64_t body_length);
    KiottyNetCode checkUploadPiece(const HttpUpload& upload, size_t length);
    HttpUpload    takeUploadPiece(const HttpUpload& upload, size_t length);
    HttpUpload    refuseUpload(const HttpUpload& upload);
    HttpUpload    answerUpload(const HttpUpload& upload);
    KiottyNetCode checkUploadEnd(const HttpUpload& upload);
}
