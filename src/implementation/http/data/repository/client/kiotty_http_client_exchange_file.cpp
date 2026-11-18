#include "http/data/repository/client/kiotty_http_client_exchange.h"

#include "core/kiotty_busy_guard.h"
#include "core/kiotty_result.h"
#include "http/data/repository/body/kiotty_http_upload_rules.h"
#include "http/data/repository/client/kiotty_http_client_send.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using FileSizeResult = Result<uint64_t, KiottyNetCode>;

        KiottyNetHttpReply unsentReply(KiottyNetCode code);
        FileSizeResult     fileSizeOf(Span<const char> path);
    }

    KiottyNetHttpReply HttpClientExchange::sendFile(const ClientRequest& request,
                                                    Span<const char> path,
                                                    uint32_t timeout_milliseconds)
    {
        const BusyGuard guard(_state.busy);
        if (!guard.isHeld()) { return unsentReply(KIOTTY_NET_CODE_BUSY); }
        const FileSizeResult size = fileSizeOf(path);
        if (!size) { return unsentReply(size.code); }
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode started = start(request, size.value, deadline);
        if (started != KIOTTY_NET_CODE_SUCCESS) { return unsentReply(started); }
        const KiottyNetCode sent = sendFileBody(path, size.value, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS) { stopUpload(sent); }
        const bool is_dropped = _state.upload.mode == UploadMode::IDLE;
        if (sent != KIOTTY_NET_CODE_SUCCESS && is_dropped) { return unsentReply(sent); }
        return end(deadline);
    }

    KiottyNetCode HttpClientExchange::sendFileBody(Span<const char> path,
                                                   uint64_t length,
                                                   const Deadline& deadline)
    {
        const uint64_t piece_limit = _state.file_piece;
        uint64_t       done = 0;
        while (done < length)
        {
            const uint64_t left = length - done;
            const uint64_t piece = left < piece_limit ? left : piece_limit;
            const KiottyNetCode sent = sendFilePart(path, FileRange{ done, piece }, deadline);
            if (sent != KIOTTY_NET_CODE_SUCCESS) { return sent; }
            _state.upload = takeUploadPiece(_state.upload, static_cast<size_t>(piece));
            done += piece;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpClientExchange::sendFilePart(Span<const char> path,
                                                   FileRange part,
                                                   const Deadline& deadline)
    {
        for (;;)
        {
            const KiottyNetCode early = takeEarlyReplies(deadline);
            if (early != KIOTTY_NET_CODE_SUCCESS) { return early; }
            const TimeLeftResult left = timeLeftOf(deadline);
            if (!left) { return left.code; }
            const StreamResult stream = _line.stream();
            if (!stream) { return stream.code; }
            const KiottyNetCode sent =
                stream.value().writeFile(path, part.offset, part.length, left.value);
            if (sent != KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING) { return sent; }
        }
    }

    namespace
    {
        KiottyNetHttpReply unsentReply(KiottyNetCode code)
        {
            return KiottyNetHttpReply{ code, 0, nullptr, 0 };
        }

        FileSizeResult fileSizeOf(Span<const char> path)
        {
            InputFile file;
            const KiottyNetCode opened = file.open(path);
            const bool is_open = opened == KIOTTY_NET_CODE_SUCCESS;
            if (!is_open) { return FileSizeResult::failure(KIOTTY_NET_CODE_NOT_FOUND); }
            return FileSizeResult::success(file.size());
        }
    }
}
