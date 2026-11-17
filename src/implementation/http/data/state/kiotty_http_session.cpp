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
        , state(idleSessionState())
        , id{ 0, index }
        , is_worker_owned(false)
        , is_orphaned(false)
    {
    }

    HttpSessionState idleSessionState()
    {
        const BodyFraming no_body{ 0, BodyKind::NONE };
        const RequestFraming framing{ no_body, Persistence::KEEP_ALIVE, false };
        return HttpSessionState{
            framing,
            BodyPlan{ 0 },
            0,
            0,
            KIOTTY_NET_CODE_SUCCESS,
            KIOTTY_NET_CODE_SUCCESS,
            SessionPhase::READING,
            Persistence::KEEP_ALIVE,
            false,
        };
    }
}
