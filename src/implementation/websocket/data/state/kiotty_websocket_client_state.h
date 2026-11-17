#pragma once

#include "core/kiotty_parker.h"
#include "core/kiotty_span.h"
#include "core/pool/kiotty_pool_handle.h"
#include "http/data/repository/response/kiotty_http_reply_store.h"
#include "http/data/repository/response/kiotty_http_response_store.h"
#include "http/data/repository/upgrade/kiotty_http_upgrade_handler.h"
#include "kiotty_net_http.h"
#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct ClientUpgrade
    {
        ClientEvents            events;
        Span<const char>        request;
        Span<const char>        accept;
        Span<const char>        protocols;
        KiottyNetConnectionInfo peer;
        Parker*                 opened;
        KiottyNetCode           failure;
        bool                    has_opened;
        bool                    has_failed;
        std::atomic<uint64_t>   mask_state;
    };

    class WebSocketHandler;

    struct WebSocketClientState
    {
        WebSocketClientState();

        ClientUpgrade            upgrade;
        HttpReplyStore           reply;
        HttpResponseStore        response;
        KiottyNetHttpRequest     request;
        KiottyNetConnectionInfo  info;
        Span<char>               message;
        KiottyNetHttpLimits      limits;
        size_t                   receive_capacity;
        size_t                   push_capacity;
        KiottyNetAppContext      app_context;
        KiottyNetHttpErrorAction on_error;
        WebSocketHandler*        handler;
        PoolHandle               id;
        uint64_t                 header_deadline;
        size_t                   sent;
    };

    UpgradeSession clientUpgradeOf(WebSocketClientState& state);
}
