#pragma once

#include "core/kiotty_span.h"
#include "data/repository/http/kiotty_http_shared_headers.h"
#include "kiotty_net_base.h"

#include <atomic>

namespace kiotty::net::implementation
{
    class HttpClientHeaders
    {
    public:
        HttpClientHeaders(HttpSharedHeaders& shared, std::atomic<bool>& is_busy);

        KiottyNetCode set(Span<const char> name, Span<const char> value);
        KiottyNetCode remove(Span<const char> name);
        KiottyNetCode clear();

    private:
        HttpSharedHeaders& _shared;
        std::atomic<bool>& _is_busy;
    };
}
