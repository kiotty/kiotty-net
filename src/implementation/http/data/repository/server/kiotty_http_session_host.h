#pragma once

#include "http/data/datasource/kiotty_http_session_pool.h"
#include "http/data/repository/server/kiotty_http_notices.h"
#include "http/data/repository/server/kiotty_http_registry.h"
#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_http.h"

#include <atomic>
#include <cstddef>

namespace kiotty::net::implementation
{
    struct HttpSessionHost
    {
        HttpSessionHost();

        HttpSessionHost(const HttpSessionHost&) = delete;
        HttpSessionHost& operator=(const HttpSessionHost&) = delete;

        HttpRegistry              registry;
        HttpNotices               notices;
        HttpSessionPool           sessions;
        KiottyNetHttpLimits       limits;
        size_t                    receive_capacity;
        size_t                    send_capacity;
        size_t                    push_capacity;
        KiottyNetHttpServerHandle owner;
        KiottyNetAppContext       app_context;
        HttpUpgradeHandler*       upgrade;
        std::atomic<bool>         is_closing;
    };

    inline HttpSessionHost::HttpSessionHost()
        : registry()
        , notices()
        , sessions()
        , limits{}
        , receive_capacity(0)
        , send_capacity(0)
        , push_capacity(0)
        , owner(nullptr)
        , app_context(nullptr)
        , upgrade(nullptr)
        , is_closing(false)
    {
    }
}
