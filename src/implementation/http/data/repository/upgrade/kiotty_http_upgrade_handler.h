#pragma once

#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct HttpSession;

    class HttpUpgradeHandler
    {
    public:
        HttpUpgradeHandler() = default;
        HttpUpgradeHandler(const HttpUpgradeHandler&) = delete;
        HttpUpgradeHandler& operator=(const HttpUpgradeHandler&) = delete;

        virtual KiottyNetTransition answer(HttpSession& session, int32_t upgrade_route) = 0;
        virtual KiottyNetTransition step(HttpSession& session,
                                         KiottyNetConnectionHandle connection,
                                         KiottyNetConnectionEvent event) = 0;
        virtual void                end(HttpSession& session, KiottyNetCode reason) = 0;

    protected:
        ~HttpUpgradeHandler() = default;
    };
}
