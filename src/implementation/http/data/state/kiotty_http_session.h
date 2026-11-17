#pragma once

#include "base/data/infra/file/kiotty_input_file.h"
#include "core/kiotty_span.h"
#include "core/pool/kiotty_pool_handle.h"
#include "http/data/repository/request/kiotty_http_request_store.h"
#include "http/data/repository/response/kiotty_http_response_store.h"
#include "http/domain/entity/kiotty_http_session_state.h"
#include "kiotty_net_http.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct HttpSessionHost;

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
        HttpSession(HttpSessionHost& owner,
                    const SessionResources& resources,
                    int32_t index,
                    const SessionBuffers& buffers);

        HttpRequestStore          request;
        HttpResponseStore         response;
        HttpSessionHost&          host;
        InputFile&                file;
        std::atomic<int64_t>&     published;
        KiottyNetConnectionInfo   info;
        KiottyNetHttpRoute        route;
        KiottyNetConnectionStream stream;
        HttpSessionState          state;
        PoolHandle                id;
        bool                      is_worker_owned;
        bool                      is_orphaned;
    };

    HttpSessionState idleSessionState();
}
