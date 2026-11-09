#pragma once

#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    struct Handler
    {
        Handler(const KiottyNetConnectionHandler& user_callbacks,
                KiottyNetAppContext application,
                KiottyNetEndpoint& connection_owner);

        const KiottyNetConnectionHandler& callbacks;
        KiottyNetAppContext               app_context;
        KiottyNetEndpoint&         owner;
    };

    inline Handler::Handler(const KiottyNetConnectionHandler& user_callbacks,
                            KiottyNetAppContext application,
                            KiottyNetEndpoint& connection_owner)
        : callbacks(user_callbacks)
        , app_context(application)
        , owner(connection_owner)
    {
    }
}
