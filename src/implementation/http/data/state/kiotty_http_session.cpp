#include "http/data/state/kiotty_http_session.h"

namespace kiotty::net::implementation
{
    HttpSession::HttpSession(HttpSessionHost& owner,
                             const SessionResources& resources,
                             int32_t index,
                             const SessionBuffers& buffers)
        : request(buffers.header, buffers.headers, buffers.body)
        , response(buffers.response)
        , host(owner)
        , file(resources.file)
        , published(resources.published)
        , info{}
        , route{}
        , stream{ nullptr, 0 }
        , framing{ BodyFraming{ 0, BodyKind::NONE }, Persistence::KEEP_ALIVE, false }
        , body{ 0 }
        , header_deadline(0)
        , sent(0)
        , id{ 0, index }
        , close_reason(KIOTTY_NET_CODE_SUCCESS)
        , stream_failure(KIOTTY_NET_CODE_SUCCESS)
        , phase(SessionPhase::READING)
        , persistence(Persistence::KEEP_ALIVE)
        , is_work_requested(false)
        , is_worker_owned(false)
        , is_orphaned(false)
    {
    }
}
