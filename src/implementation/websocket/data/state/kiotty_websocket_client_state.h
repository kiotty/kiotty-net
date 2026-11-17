#pragma once

#include "core/kiotty_parker.h"
#include "core/kiotty_span.h"
#include "kiotty_net_websocket.h"
#include "websocket/domain/entity/kiotty_websocket_target.h"

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
    };
}
