#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "data/repository/http/kiotty_http_session.h"
#include "kiotty_net_http.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    using PieceResult = Result<Span<const char>, KiottyNetCode>;

    KiottyNetCode attachFile(HttpSession& session,
                             KiottyNetHttpContentType type,
                             Span<const char> path);
    PieceResult   pieceAt(HttpSession& session, size_t offset);
    void          detachFile(HttpSession& session);
}
