#include "domain/entity/kiotty_action.h"

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetConnectionAction actionOf(KiottyNetActionType type, uint32_t timeout_milliseconds);
    }

    KiottyNetConnectionAction thenHold(uint32_t timeout_milliseconds)
    {
        return actionOf(KIOTTY_NET_ACTION_TYPE_HOLD, timeout_milliseconds);
    }

    KiottyNetConnectionAction thenRead(size_t exact, uint32_t timeout_milliseconds)
    {
        KiottyNetConnectionAction action =
            actionOf(KIOTTY_NET_ACTION_TYPE_READ, timeout_milliseconds);
        action.exact = exact;
        return action;
    }

    KiottyNetConnectionAction thenReadUntil(KiottyNetSpan delimiter,
                                            size_t max,
                                            uint32_t timeout_milliseconds)
    {
        KiottyNetConnectionAction action =
            actionOf(KIOTTY_NET_ACTION_TYPE_READ_UNTIL, timeout_milliseconds);
        action.delimiter = delimiter;
        action.max = max;
        return action;
    }

    KiottyNetConnectionAction thenWrite(KiottyNetSpan data, uint32_t timeout_milliseconds)
    {
        KiottyNetConnectionAction action =
            actionOf(KIOTTY_NET_ACTION_TYPE_WRITE, timeout_milliseconds);
        action.data = data;
        return action;
    }

    KiottyNetConnectionAction thenWork()
    {
        return actionOf(KIOTTY_NET_ACTION_TYPE_START_WORK, 0);
    }

    KiottyNetConnectionAction thenClose(KiottyNetCode reason)
    {
        KiottyNetConnectionAction action = actionOf(KIOTTY_NET_ACTION_TYPE_CLOSE, 0);
        action.reason = reason;
        return action;
    }

    KiottyNetConnectionDecision acceptConnection(KiottyNetConnectionContext connection_context)
    {
        return KiottyNetConnectionDecision{ true, connection_context };
    }

    KiottyNetConnectionDecision ignoreConnection()
    {
        return KiottyNetConnectionDecision{ false, nullptr };
    }

    namespace
    {
        KiottyNetConnectionAction actionOf(KiottyNetActionType type, uint32_t timeout_milliseconds)
        {
            KiottyNetConnectionAction action{};
            action.type = type;
            action.timeout_milliseconds = timeout_milliseconds;
            return action;
        }
    }
}
