#include "domain/usecases/http/kiotty_http_static.h"

#include "core/kiotty_fixed_string.h"
#include "data/repository/http/kiotty_http_file_body.h"
#include "domain/usecases/http/kiotty_http_route_path.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t FORBIDDEN = KIOTTY_NET_HTTP_STATUS_FORBIDDEN;
        constexpr int32_t NOT_FOUND = KIOTTY_NET_HTTP_STATUS_NOT_FOUND;

        using JoinedPath = FixedString<KIOTTY_NET_MAX_PATH_LENGTH>;

        void answerFile(HttpSession& session,
                        KiottyNetHttpContentType type,
                        Span<const char> location);
        void answerDirectory(HttpSession& session, const StaticTarget& target);
        void answerStatus(HttpSession& session, int32_t status);
    }

    void answerStatic(HttpSession& session, const StaticTarget& target)
    {
        switch (target.kind)
        {
        case StaticKind::NONE:
            return;
        case StaticKind::MEMORY:
            (void)session.response.setBody(target.type, target.memory, BodyStorage::REFERENCE);
            return;
        case StaticKind::FILE:
            answerFile(session, target.type, target.location);
            return;
        case StaticKind::DIRECTORY:
            answerDirectory(session, target);
            return;
        }
    }

    namespace
    {
        void answerFile(HttpSession& session,
                        KiottyNetHttpContentType type,
                        Span<const char> location)
        {
            const KiottyNetCode attached = attachFile(session, type, location);
            if (attached != KIOTTY_NET_CODE_SUCCESS) { answerStatus(session, NOT_FOUND); }
        }

        void answerDirectory(HttpSession& session, const StaticTarget& target)
        {
            const KiottyNetSpan path = session.request.request().path;
            const Span<const char> request_path{ path.data, path.length };
            const Span<const char> rest = request_path.skip(target.prefix.length);
            const bool is_safe = isSafeRelativePath(rest);
            JoinedPath joined;
            const bool is_joined = is_safe && joined.assign(target.location) && joined.append(rest);
            const bool is_missing = is_safe && !is_joined;
            const KiottyNetHttpContentType type = KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
            if (!is_safe) { answerStatus(session, FORBIDDEN); }
            if (is_missing) { answerStatus(session, NOT_FOUND); }
            if (is_joined) { answerFile(session, type, joined.span()); }
        }

        void answerStatus(HttpSession& session, int32_t status)
        {
            session.response.begin();
            (void)session.response.setStatus(status);
        }
    }
}
