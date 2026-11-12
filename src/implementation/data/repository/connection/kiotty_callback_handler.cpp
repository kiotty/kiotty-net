#include "data/repository/connection/kiotty_callback_handler.h"

#include "data/repository/connection/kiotty_caller.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t NO_MACHINE_STATE = 0;

        KiottyNetConnectionAction actionOf(KiottyNetActionType type);
    }

    CallbackHandler::CallbackHandler(const KiottyNetConnectionHandler& callbacks,
                                     KiottyNetAppContext app_context,
                                     KiottyNetEndpoint& connection_owner)
        : Handler(connection_owner)
        , _callbacks(callbacks)
        , _app_context(app_context)
    {
    }

    KiottyNetCode CallbackHandler::validate() const
    {
        return KIOTTY_NET_CODE_SUCCESS;
    }

    int32_t CallbackHandler::initialState() const
    {
        return NO_MACHINE_STATE;
    }

    KiottyNetConnectionDecision CallbackHandler::admit(const KiottyNetConnectionInfo& info) const
    {
        return _callbacks.onConnected(_app_context, info);
    }

    KiottyNetConnectionAction CallbackHandler::dispatch(const Caller& caller,
                                                        const KiottyNetConnectionEvent& event) const
    {
        const KiottyNetConnectionContext context = caller.context;
        switch (event.type)
        {
        case KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED:
            return _callbacks.onReady(context, caller.handle());
        case KIOTTY_NET_CONNECTION_EVENT_TYPE_READ:
            return _callbacks.onRead(context, caller.handle(), event.data);
        case KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN:
            return _callbacks.onWritten(context, caller.handle(), event.length);
        case KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT:
            return _callbacks.onTimeout(context, caller.handle());
        case KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED:
            return startWork(event, caller);
        case KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED:
            return disconnect(event, caller);
        }
        return actionOf(KIOTTY_NET_ACTION_TYPE_HOLD);
    }

    void CallbackHandler::report(const KiottyNetConnectionInfo& info,
                                 KiottyNetCode code,
                                 const char* message) const
    {
        _callbacks.onError(_app_context, info, code, message);
    }

    KiottyNetConnectionAction CallbackHandler::startWork(const KiottyNetConnectionEvent& event,
                                                         const Caller& caller) const
    {
        _callbacks.onWorkStarted(caller.context, event.stream);
        return actionOf(KIOTTY_NET_ACTION_TYPE_START_WORK);
    }

    KiottyNetConnectionAction CallbackHandler::disconnect(const KiottyNetConnectionEvent& event,
                                                          const Caller& caller) const
    {
        const KiottyNetCode reason = event.reason;
        _callbacks.onDisconnected(_app_context, caller.context, reason);
        return actionOf(KIOTTY_NET_ACTION_TYPE_HOLD);
    }

    namespace
    {
        KiottyNetConnectionAction actionOf(KiottyNetActionType type)
        {
            KiottyNetConnectionAction action{};
            action.type = type;
            return action;
        }
    }
}
