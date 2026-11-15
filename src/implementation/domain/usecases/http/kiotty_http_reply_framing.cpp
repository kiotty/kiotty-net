#include "domain/usecases/http/kiotty_http_reply_framing.h"

#include "core/kiotty_codes.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t       FIRST_SUCCESS_STATUS = 200;
        constexpr int32_t       SWITCHING_STATUS = KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS;
        constexpr int32_t       NO_CONTENT_STATUS = KIOTTY_NET_HTTP_STATUS_NO_CONTENT;
        constexpr int32_t       NOT_MODIFIED_STATUS = KIOTTY_NET_HTTP_STATUS_NOT_MODIFIED;

        ReplyFraming  frameWithoutBody(ReplyPhase phase, Persistence persistence);
        ReplyBodyKind replyKindOf(BodyKind kind);
        bool          isAcceptedStatus(int32_t status);
        bool          isBodyless(KiottyNetHttpMethod method, int32_t status);
    }

    ReplyFramingResult selectReplyFraming(KiottyNetHttpMethod method,
                                          const StatusLine& status_line,
                                          Span<const KiottyNetHttpHeader> headers)
    {
        const int32_t status = status_line.status;
        if (!isAcceptedStatus(status)) { return ReplyFramingResult::failure(PROTOCOL_ERROR); }
        const Persistence persistence = decidePersistence(status_line.version, headers);
        const bool is_interim = status < FIRST_SUCCESS_STATUS;
        const ReplyPhase phase = is_interim ? ReplyPhase::INTERIM : ReplyPhase::FINAL;
        const ReplyFraming bodyless = frameWithoutBody(phase, persistence);
        if (isBodyless(method, status)) { return ReplyFramingResult::success(bodyless); }
        const FramingResult framing = selectBodyFraming(headers);
        if (!framing) { return ReplyFramingResult::failure(PROTOCOL_ERROR); }
        const ReplyBodyKind kind = replyKindOf(framing.value.kind);
        const bool is_until_close = kind == ReplyBodyKind::UNTIL_CLOSE;
        const Persistence kept = is_until_close ? Persistence::CLOSE : persistence;
        const ReplyFraming framed{ framing.value.length, kind, kept, ReplyPhase::FINAL };
        return ReplyFramingResult::success(framed);
    }

    namespace
    {
        ReplyFraming  frameWithoutBody(ReplyPhase phase, Persistence persistence)
        {
            return ReplyFraming{ 0, ReplyBodyKind::NONE, persistence, phase };
        }

        ReplyBodyKind replyKindOf(BodyKind kind)
        {
            switch (kind)
            {
            case BodyKind::LENGTH:
                return ReplyBodyKind::LENGTH;
            case BodyKind::CHUNKED:
                return ReplyBodyKind::CHUNKED;
            case BodyKind::NONE:
                break;
            }
            return ReplyBodyKind::UNTIL_CLOSE;
        }

        bool isAcceptedStatus(int32_t status)
        {
            const bool is_in_range = status >= FIRST_STATUS && status <= LAST_STATUS;
            return is_in_range && status != SWITCHING_STATUS;
        }

        bool isBodyless(KiottyNetHttpMethod method, int32_t status)
        {
            if (method == KIOTTY_NET_HTTP_METHOD_HEAD) { return true; }
            if (status < FIRST_SUCCESS_STATUS) { return true; }
            return status == NO_CONTENT_STATUS || status == NOT_MODIFIED_STATUS;
        }
    }
}
