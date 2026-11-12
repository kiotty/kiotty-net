#pragma once

#include "data/repository/http/kiotty_http_session_pool.h"
#include "data/repository/http/kiotty_http_site.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class HttpWork
    {
    public:
        explicit HttpWork(HttpSite& site);

        SessionResult   fill(int64_t id);
        KiottyNetCode   start(int64_t id);
        KiottyNetResult write(int64_t id, KiottyNetSpan data, uint32_t timeout_milliseconds);
        KiottyNetCode   finishWithResponse(int64_t id);
        KiottyNetCode   finishWithClose(int64_t id, KiottyNetCode reason);
        bool            isConnected(int64_t id);

    private:
        SessionResult working(int64_t id);

        HttpSite& _site;
    };
}
