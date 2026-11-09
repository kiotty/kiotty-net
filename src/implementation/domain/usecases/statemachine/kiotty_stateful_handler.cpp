#include "domain/usecases/statemachine/kiotty_stateful_handler.h"

#include "data/repository/connection/kiotty_caller.h"

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const UNHANDLED_EVENT_MESSAGE = "statemachine: no rule for the state and event";
        const char* const ILLEGAL_TRANSITION_MESSAGE = "statemachine: next state is not allowed";
        const char* const SESSION_EXHAUSTED_MESSAGE = "statemachine: no connection context";

        KiottyNetConnectionAction   holdAction();
        KiottyNetConnectionAction   closeAction(KiottyNetCode reason);
        KiottyNetConnectionDecision acceptedDecision(KiottyNetConnectionContext context);
        KiottyNetConnectionDecision ignoredDecision();
    }

    StatefulHandler::StatefulHandler(const KiottyNetStatefulAppContext& stateful,
                                     KiottyNetEndpoint& connection_owner)
        : Handler(connection_owner)
        , _stateful(stateful)
    {
    }

    KiottyNetCode StatefulHandler::validate() const
    {
        return checkRules(_stateful.machine);
    }

    int32_t StatefulHandler::initialState() const
    {
        return _stateful.machine->initial;
    }

    KiottyNetConnectionDecision StatefulHandler::admit(const KiottyNetConnectionInfo& info) const
    {
        if (_stateful.createConnectionContext == nullptr) { return acceptedDecision(nullptr); }
        const KiottyNetConnectionContext session =
            _stateful.createConnectionContext(_stateful.app_context, info);
        if (session == nullptr)
        {
            report(info, KIOTTY_NET_CODE_SESSION_EXHAUSTED, SESSION_EXHAUSTED_MESSAGE);
            return ignoredDecision();
        }
        return acceptedDecision(session);
    }

    KiottyNetConnectionAction StatefulHandler::dispatch(const Caller& caller,
                                                        const KiottyNetConnectionEvent& event) const
    {
        const RuleResult found = findRule(*_stateful.machine, caller.machine_state, event.type);
        const bool is_closed = event.type == KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED;
        if (is_closed) { return finish(caller, event, found); }
        if (!found)
        {
            return refuse(caller, KIOTTY_NET_CODE_UNHANDLED_EVENT, UNHANDLED_EVENT_MESSAGE);
        }
        const KiottyNetRule& rule = found.value();
        const KiottyNetTransition transition =
            rule.transition_action(caller.context, caller.handle(), event);
        if (!isAllowed(rule, transition.next))
        {
            return refuse(caller, KIOTTY_NET_CODE_ILLEGAL_TRANSITION, ILLEGAL_TRANSITION_MESSAGE);
        }
        caller.machine_state = transition.next;
        return transition.entry_action;
    }

    void StatefulHandler::report(const KiottyNetConnectionInfo& info,
                                 KiottyNetCode code,
                                 const char* message) const
    {
        if (_stateful.onError == nullptr) { return; }
        _stateful.onError(_stateful.app_context, info, code, message);
    }

    KiottyNetConnectionAction StatefulHandler::finish(const Caller& caller,
                                                      const KiottyNetConnectionEvent& event,
                                                      const RuleResult& found) const
    {
        if (found)
        {
            const KiottyNetRule& rule = found.value();
            static_cast<void>(rule.transition_action(caller.context, caller.handle(), event));
        }
        release(caller);
        return holdAction();
    }

    KiottyNetConnectionAction StatefulHandler::refuse(const Caller& caller,
                                                      KiottyNetCode code,
                                                      const char* message) const
    {
        report(caller.info, code, message);
        return closeAction(code);
    }

    void StatefulHandler::release(const Caller& caller) const
    {
        if (_stateful.releaseConnectionContext == nullptr) { return; }
        if (caller.context == nullptr) { return; }
        _stateful.releaseConnectionContext(_stateful.app_context, caller.context, caller.handle());
    }

    namespace
    {
        KiottyNetConnectionAction holdAction()
        {
            KiottyNetConnectionAction action{};
            action.type = KIOTTY_NET_ACTION_TYPE_HOLD;
            return action;
        }

        KiottyNetConnectionAction closeAction(KiottyNetCode reason)
        {
            KiottyNetConnectionAction action{};
            action.type = KIOTTY_NET_ACTION_TYPE_CLOSE;
            action.as.close.reason = reason;
            return action;
        }

        KiottyNetConnectionDecision acceptedDecision(KiottyNetConnectionContext context)
        {
            KiottyNetConnectionDecision decision{};
            decision.accepted = true;
            decision.connection_context = context;
            return decision;
        }

        KiottyNetConnectionDecision ignoredDecision()
        {
            KiottyNetConnectionDecision decision{};
            decision.accepted = false;
            return decision;
        }
    }
}
