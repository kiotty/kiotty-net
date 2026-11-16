#pragma once

#include "base/data/infra/file/kiotty_input_file.h"
#include "core/kiotty_span.h"
#include "core/pool/kiotty_pool_handle.h"
#include "http/data/repository/request/kiotty_http_request_store.h"
#include "http/data/repository/response/kiotty_http_response_store.h"
#include "http/domain/entity/kiotty_http_message_framing.h"
#include "kiotty_net_http.h"

#include <atomic>
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
        SENDING,
        UPGRADING,
        SOCKET
    };

    struct SessionBuffers
    {
        Span<char>                header;
        Span<KiottyNetHttpHeader> headers;
        Span<char>                body;
        Span<char>                response;
    };

    struct SessionResources
    {
        InputFile&            file;
        std::atomic<int64_t>& published;
    };

    struct HttpSession
    {
        HttpSession(HttpSite& owner,
                    const SessionResources& resources,
                    int32_t index,
                    const SessionBuffers& buffers);

        HttpRequestStore          request;
        HttpResponseStore         response;
        HttpSite&                 site;
        InputFile&                file;
        std::atomic<int64_t>&     published;
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
        bool                      has_body;
    };
}
