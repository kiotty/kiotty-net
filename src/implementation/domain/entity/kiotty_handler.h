#pragma once

#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    struct Caller;

    class Handler
    {
    public:
        explicit Handler(KiottyNetEndpoint& connection_owner);
        virtual ~Handler() = default;

        Handler(const Handler&) = delete;
        Handler& operator=(const Handler&) = delete;

        virtual KiottyNetCode               validate() const = 0;
        virtual int32_t                     initialState() const = 0;
        virtual KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo& info) const = 0;
        virtual KiottyNetConnectionAction   dispatch(
            const Caller& caller, const KiottyNetConnectionEvent& event) const = 0;
        virtual void                        report(const KiottyNetConnectionInfo& info,
                                                   KiottyNetCode code,
                                                   const char* message) const = 0;

        KiottyNetEndpoint& owner;
    };

    inline Handler::Handler(KiottyNetEndpoint& connection_owner)
        : owner(connection_owner)
    {
    }
}
