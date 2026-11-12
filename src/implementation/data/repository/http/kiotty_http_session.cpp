#include "data/repository/http/kiotty_http_session.h"

namespace kiotty::net::implementation
{
    HttpSession::HttpSession(HttpSite& owner, int32_t index, const SessionBuffers& buffers)
        : request(buffers.header, buffers.headers, buffers.body)
        , response(buffers.response)
        , site(owner)
        , info{}
        , body_remaining(0)
        , header_deadline(0)
        , sent(0)
        , id{ 0, index }
        , close_reason(KIOTTY_NET_CODE_SUCCESS)
        , phase(SessionPhase::READING)
        , persistence(Persistence::KEEP_ALIVE)
    {
    }
}
