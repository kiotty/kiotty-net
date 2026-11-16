#include "http/data/repository/response/kiotty_http_send_rules.h"

namespace kiotty::net::implementation
{
    namespace
    {
        bool isFinalPiece(const ResponseFrame& frame, size_t end);
    }

    SendStep sendStepOf(const ResponseFrame& frame,
                        size_t sent,
                        Span<const char> chunk,
                        Persistence persistence)
    {
        if (chunk.empty()) { return SendStep{ chunk, SendKind::DONE }; }
        const bool is_closing = persistence == Persistence::CLOSE;
        const size_t end = sent + chunk.length;
        const bool is_farewell = is_closing && isFinalPiece(frame, end);
        const SendKind kind = is_farewell ? SendKind::FAREWELL : SendKind::WRITE;
        return SendStep{ chunk, kind };
    }

    AfterResponse afterResponseOf(Persistence persistence)
    {
        const bool is_kept = persistence == Persistence::KEEP_ALIVE;
        return is_kept ? AfterResponse::NEXT_EXCHANGE : AfterResponse::CLOSE;
    }

    namespace
    {
        bool isFinalPiece(const ResponseFrame& frame, size_t end)
        {
            const FilePiece file = frame.filePieceAt(end, 1);
            if (file.is_file) { return false; }
            const Span<const char> rest = frame.chunkAt(end, 1);
            return rest.empty();
        }
    }
}
