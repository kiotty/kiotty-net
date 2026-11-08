#pragma once

#include "data/infra/tls/kiotty_tls_profile.h"
#include "data/repository/connection/kiotty_acceptor.h"
#include "data/repository/connection/kiotty_connection_host.h"
#include "data/infra/event/kiotty_event_listener.h"
#include "domain/entity/kiotty_handler.h"

namespace kiotty::net::implementation
{
    struct LoopParts
    {
        ConnectionHost& host;
        EventListener&  listener;
        Acceptor&       acceptor;
        TlsProfile&     profile;
        const Handler&  handler;
    };

    static_assert(sizeof(LoopParts) == 5 * sizeof(void*), "LoopParts: unexpected padding");
}
