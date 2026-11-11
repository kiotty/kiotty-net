#include "domain/entity/kiotty_connection_event.h"

namespace kiotty::net::implementation
{
    namespace
    {
        KiottyNetConnectionEvent eventOf(KiottyNetConnectionEventType type);
    }

    KiottyNetConnectionEvent connectedEvent()
    {
        return eventOf(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED);
    }

    KiottyNetConnectionEvent readEvent(KiottyNetSpan data)
    {
        KiottyNetConnectionEvent event = eventOf(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ);
        event.data = data;
        return event;
    }

    KiottyNetConnectionEvent writtenEvent(size_t length)
    {
        KiottyNetConnectionEvent event = eventOf(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN);
        event.length = length;
        return event;
    }

    KiottyNetConnectionEvent workStartedEvent(KiottyNetConnectionStream stream)
    {
        KiottyNetConnectionEvent event = eventOf(KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED);
        event.stream = stream;
        return event;
    }

    KiottyNetConnectionEvent timeoutEvent()
    {
        return eventOf(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT);
    }

    KiottyNetConnectionEvent closedEvent(KiottyNetCode reason)
    {
        KiottyNetConnectionEvent event = eventOf(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED);
        event.reason = reason;
        return event;
    }

    namespace
    {
        KiottyNetConnectionEvent eventOf(KiottyNetConnectionEventType type)
        {
            KiottyNetConnectionEvent event{};
            event.type = type;
            return event;
        }
    }
}
