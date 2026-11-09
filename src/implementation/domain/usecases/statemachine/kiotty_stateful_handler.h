#pragma once

#include "domain/entity/kiotty_handler.h"
#include "domain/usecases/statemachine/kiotty_rule_table.h"
#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class StatefulHandler final : public Handler
    {
    public:
        StatefulHandler(const KiottyNetStatefulAppContext& stateful,
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
        KiottyNetConnectionAction finish(const Caller& caller,
                                         const KiottyNetConnectionEvent& event,
                                         const RuleResult& found) const;
        KiottyNetConnectionAction refuse(const Caller& caller,
                                         KiottyNetCode code,
                                         const char* message) const;
        void                      release(const Caller& caller) const;

        const KiottyNetStatefulAppContext& _stateful;
    };
}
