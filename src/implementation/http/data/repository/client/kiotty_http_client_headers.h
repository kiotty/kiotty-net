#pragma once

#include "core/kiotty_busy_guard.h"
#include "core/kiotty_span.h"
#include "http/data/repository/header/kiotty_http_shared_headers.h"
#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    class HttpClientHeaders
    {
    public:
        HttpClientHeaders(HttpSharedHeaders& shared, BusyFlag& is_busy);

        KiottyNetCode set(Span<const char> name, Span<const char> value);
        KiottyNetCode remove(Span<const char> name);
        KiottyNetCode clear();

    private:
        HttpSharedHeaders& _shared;
        BusyFlag&          _is_busy;
    };
}
