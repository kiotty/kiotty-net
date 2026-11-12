#include "data/repository/http/kiotty_http_file_body.h"

#include "data/repository/http/kiotty_http_site.h"
#include "domain/usecases/http/kiotty_http_content_type.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetHttpContentType resolvedTypeOf(KiottyNetHttpContentType type,
                                                Span<const char> path);
    }

    KiottyNetCode attachFile(HttpSession& session,
                             KiottyNetHttpContentType type,
                             Span<const char> path)
    {
        const KiottyNetCode opened = session.file.open(path);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return KIOTTY_NET_CODE_NOT_FOUND; }
        const uint64_t size = session.file.size();
        const bool is_too_large = static_cast<uint64_t>(static_cast<size_t>(size)) != size;
        if (is_too_large) { session.file.close(); }
        if (is_too_large) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetSpan length{ nullptr, static_cast<size_t>(size) };
        const KiottyNetHttpContentType resolved = resolvedTypeOf(type, path);
        const KiottyNetCode set = session.response.setBody(resolved, length, BodyStorage::FILE);
        if (set != KIOTTY_NET_CODE_SUCCESS) { session.file.close(); }
        return set;
    }

    PieceResult pieceAt(HttpSession& session, size_t offset)
    {
        const size_t capacity = session.site.send_capacity;
        const ResponseFrame frame = session.response.frame();
        const FilePiece piece = frame.filePieceAt(offset, capacity);
        if (!piece.is_file) { return PieceResult::success(frame.chunkAt(offset, capacity)); }
        const FileReadResult read = session.file.read(piece.file_offset, piece.target);
        const bool is_whole = read && read.value == piece.target.length;
        if (!is_whole) { return PieceResult::failure(KIOTTY_NET_CODE_NOT_FOUND); }
        return PieceResult::success(Span<const char>{ piece.target.data, piece.target.length });
    }

    void detachFile(HttpSession& session)
    {
        session.file.close();
    }

    namespace
    {
        KiottyNetHttpContentType resolvedTypeOf(KiottyNetHttpContentType type,
                                                Span<const char> path)
        {
            const bool is_by_extension = type == KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
            return is_by_extension ? contentTypeOfPath(path) : type;
        }
    }
}
