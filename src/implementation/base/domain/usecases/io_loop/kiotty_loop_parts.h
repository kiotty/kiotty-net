#pragma once

#include "base/data/infra/event/kiotty_event_listener.h"
#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "base/data/repository/connection/kiotty_acceptor.h"
#include "base/data/repository/connection/kiotty_connection_host.h"
#include "base/domain/entity/kiotty_handler.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    struct LoopParts
    {
        ConnectionHost& host;
        EventListener&  listener;
        Acceptor&       acceptor;
        TlsProfile&     profile;
        const Handler&  handler;
        size_t          send_timeout_milliseconds;
    };

    static_assert(sizeof(LoopParts) == 5 * sizeof(void*) + sizeof(size_t),
                  "LoopParts: unexpected padding");
}
