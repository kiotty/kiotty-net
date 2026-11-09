#pragma once

#include "domain/entity/kiotty_handler.h"
#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class CallbackHandler final : public Handler
    {
    public:
        CallbackHandler(const KiottyNetConnectionHandler& callbacks,
                        KiottyNetAppContext app_context,
                        KiottyNetEndpoint& connection_owner);

        KiottyNetCode               validate() const override;
        int32_t                     initialState() const override;
        KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo& info) const override;
        KiottyNetConnectionAction   dispatch(const Caller& caller,
                                             const KiottyNetConnectionEvent& event) const override;
        void                        report(const KiottyNetConnectionInfo& info,
                                           KiottyNetCode code,
                                           const char* message) const override;

    private:
        KiottyNetConnectionAction startWork(const KiottyNetConnectionEvent& event,
                                            const Caller& caller) const;
        KiottyNetConnectionAction disconnect(const KiottyNetConnectionEvent& event,
                                             const Caller& caller) const;

        const KiottyNetConnectionHandler& _callbacks;
        KiottyNetAppContext               _app_context;
    };
}
