#pragma once

#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    struct Handler
    {
        Handler(const KiottyNetConnectionHandler& user_callbacks, KiottyNetAppContext application, KiottyNetConnectionOwner& connection_owner);

        const KiottyNetConnectionHandler& callbacks;
        KiottyNetAppContext               app_context;
        KiottyNetConnectionOwner&         owner;
    };

    inline Handler::Handler(const KiottyNetConnectionHandler& user_callbacks, KiottyNetAppContext application, KiottyNetConnectionOwner& connection_owner)
        : callbacks(user_callbacks)
        , app_context(application)
        , owner(connection_owner)
    {
    }
}
