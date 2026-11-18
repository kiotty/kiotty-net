#include "http/data/repository/body/kiotty_http_upload_rules.h"

#include "core/kiotty_codes.h"

namespace kiotty::net::implementation
{
    namespace
    {
        bool isSending(const HttpUpload& upload);
    }

    HttpUpload idleUpload()
    {
        return HttpUpload{ 0, KIOTTY_NET_HTTP_METHOD_GET, UploadMode::IDLE };
    }

    HttpUpload startUpload(KiottyNetHttpMethod method, uint64_t body_length)
    {
        const bool is_chunked = body_length == CHUNKED_BODY_LENGTH;
        if (is_chunked) { return HttpUpload{ 0, method, UploadMode::CHUNKED }; }
        return HttpUpload{ body_length, method, UploadMode::LENGTH };
    }

    KiottyNetCode checkUploadPiece(const HttpUpload& upload, size_t length)
    {
        if (!isSending(upload)) { return INVALID; }
        if (length > RESULT_MAXIMUM_BYTES) { return INVALID; }
        const bool is_counted = upload.mode == UploadMode::LENGTH;
        const bool is_over = is_counted && length > upload.left;
        if (is_over) { return INVALID; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    HttpUpload takeUploadPiece(const HttpUpload& upload, size_t length)
    {
        HttpUpload taken = upload;
        const bool is_counted = upload.mode == UploadMode::LENGTH;
        if (is_counted) { taken.left -= length; }
        return taken;
    }

    HttpUpload refuseUpload(const HttpUpload& upload)
    {
        return HttpUpload{ 0, upload.method, UploadMode::REFUSED };
    }

    KiottyNetCode checkUploadEnd(const HttpUpload& upload)
    {
        if (!isSending(upload)) { return INVALID; }
        const bool is_short = upload.mode == UploadMode::LENGTH && upload.left > 0;
        if (is_short) { return INVALID; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    namespace
    {
        bool isSending(const HttpUpload& upload)
        {
            return upload.mode == UploadMode::LENGTH || upload.mode == UploadMode::CHUNKED;
        }
    }
}
