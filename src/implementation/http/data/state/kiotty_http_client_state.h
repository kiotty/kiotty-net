#pragma once

#include "core/kiotty_busy_guard.h"
#include "core/kiotty_span.h"
#include "http/data/repository/body/kiotty_http_body_reader.h"
#include "http/data/repository/header/kiotty_http_shared_headers.h"
#include "http/data/repository/response/kiotty_http_reply_store.h"
#include "http/domain/entity/kiotty_http_message_framing.h"
#include "http/domain/entity/kiotty_http_request_head.h"
#include "http/domain/entity/kiotty_http_upload.h"
#include "kiotty_net_base.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    struct HttpClientTarget
    {
        KiottyNetAddress address;
        HostField        host;
        bool             is_set;
    };

    struct HttpClientState
    {
        HttpClientState();

        HttpSharedHeaders shared;
        HttpReplyStore    reply;
        HttpBodyReader    body;
        HttpUpload        upload;
        HttpClientTarget  target;
        Span<char>        head_area;
        size_t            max_headers;
        size_t            file_piece;
        Persistence      persistence;
        BusyFlag          busy;
    };

    void dropReply(HttpClientState& state);
}
