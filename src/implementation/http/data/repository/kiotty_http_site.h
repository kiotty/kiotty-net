#pragma once

#include "http/data/datasource/kiotty_http_session_pool.h"
#include "http/data/repository/kiotty_http_notices.h"
#include "http/data/repository/kiotty_http_registry.h"
#include "kiotty_net_http.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/kiotty_websocket_socket.h"

#include <atomic>
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
        size_t                    push_capacity;
        KiottyNetHttpServerHandle owner;
        KiottyNetAppContext       app_context;
        KiottyNetWebSocketEndpoint* socket_owner;
        PushLane*                 lane;
        ClientUpgrade             upgrade;
        SocketRole                role;
        std::atomic<uint64_t>     mask_state;
        std::atomic<bool>         is_closing;
    };

    inline HttpSite::HttpSite()
        : registry()
        , notices()
        , sessions()
        , limits{}
        , receive_capacity(0)
        , send_capacity(0)
        , push_capacity(0)
        , owner(nullptr)
        , app_context(nullptr)
        , socket_owner(nullptr)
        , lane(nullptr)
        , upgrade{}
        , role(SocketRole::SERVER)
        , mask_state(0)
        , is_closing(false)
    {
    }
}
