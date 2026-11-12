#pragma once

#include "data/repository/http/kiotty_http_notices.h"
#include "data/repository/http/kiotty_http_registry.h"
#include "data/repository/http/kiotty_http_session_pool.h"
#include "kiotty_net_http.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    struct HttpSite
    {
        HttpSite();

        HttpSite(const HttpSite&) = delete;
        HttpSite& operator=(const HttpSite&) = delete;

        HttpRegistry              registry;
        HttpNotices               notices;
        HttpSessionPool           sessions;
        KiottyNetHttpLimits       limits;
        size_t                    receive_capacity;
        size_t                    send_capacity;
        KiottyNetHttpServerHandle owner;
    };

    inline HttpSite::HttpSite()
        : registry()
        , notices()
        , sessions()
        , limits{}
        , receive_capacity(0)
        , send_capacity(0)
        , owner(nullptr)
    {
    }
}
