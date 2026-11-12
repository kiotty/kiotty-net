#pragma once

#include "core/kiotty_span.h"
#include "data/repository/http/kiotty_http_session.h"
#include "kiotty_net_http.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class HttpState : int32_t
    {
        REQUEST_LINE,
        HEADER_LINE,
        BODY,
        CHUNK_SIZE,
        CHUNK_DATA,
        CHUNK_END,
        TRAILER,
        CONTINUING,
        WORKING,
        SENDING,
        CLOSING
    };

    HttpSession&        sessionOf(KiottyNetConnectionContext context);
    Span<const char>    lineOf(KiottyNetSpan data);
    KiottyNetTransition beginExchange(HttpSession& session,
                                      KiottyNetConnectionHandle connection,
                                      uint32_t wait_milliseconds);
    KiottyNetTransition readLine(HttpSession& session,
                                 HttpState state,
                                 uint32_t timeout_milliseconds);
    KiottyNetTransition readBody(HttpSession& session, HttpState state);
    KiottyNetTransition reject(HttpSession& session, KiottyNetHttpStatus status);
    KiottyNetTransition dispatch(HttpSession& session);
    KiottyNetTransition startWork(HttpSession& session, KiottyNetConnectionStream stream);
    KiottyNetTransition sendNext(HttpSession& session, KiottyNetConnectionHandle connection);
    KiottyNetTransition closeWith(KiottyNetCode reason);
    void                startHeaderClock(HttpSession& session);
    uint32_t            headerTimeLeft(const HttpSession& session);
}
