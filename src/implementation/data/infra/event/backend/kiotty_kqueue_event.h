#pragma once

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_KQUEUE

#include "data/infra/event/backend/kiotty_readiness_event.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <vector>

namespace kiotty::net::implementation
{
    struct EventListenerState : ReadinessState
    {
        EventListenerState();

        int                        queue;
        std::vector<struct kevent> entries;
    };

    inline EventListenerState::EventListenerState()
        : ReadinessState()
        , queue(-1)
        , entries()
    {
    }
}

#endif
