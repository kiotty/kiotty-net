#pragma once

#include "core/kiotty_span.h"
#include "core/pool/kiotty_pool_handle.h"
#include "data/repository/http/kiotty_http_request_store.h"
#include "data/repository/http/kiotty_http_response_store.h"
#include "domain/usecases/http/kiotty_http_message_framing.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct HttpSite;

    enum class SessionPhase : uint8_t
    {
        READING,
        FILLING,
        WORKING,
        STREAMING,
        SENDING
    };

    struct SessionBuffers
    {
        Span<char>                header;
        Span<KiottyNetHttpHeader> headers;
        Span<char>                body;
        Span<char>                response;
    };

    struct HttpSession
    {
        HttpSession(HttpSite& owner, int32_t index, const SessionBuffers& buffers);

        HttpRequestStore          request;
        HttpResponseStore         response;
        HttpSite&                 site;
        KiottyNetConnectionInfo   info;
        KiottyNetHttpRoute        route;
        KiottyNetConnectionStream stream;
        uint64_t                  body_remaining;
        uint64_t                  header_deadline;
        size_t                    sent;
        PoolHandle                id;
        KiottyNetCode             close_reason;
        KiottyNetCode             stream_failure;
        SessionPhase              phase;
        Persistence               persistence;
        bool                      is_work_requested;
        bool                      is_worker_owned;
        bool                      is_orphaned;
    };
}
