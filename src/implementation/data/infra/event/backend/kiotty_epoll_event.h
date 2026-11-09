#pragma once

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_EPOLL

#include "data/infra/event/backend/kiotty_readiness_event.h"

#include <sys/epoll.h>

#include <vector>

namespace kiotty::net::implementation
{
    struct EventListenerState : ReadinessState
    {
        EventListenerState();

        int                      epoll;
        int                      wake;
        std::vector<epoll_event> entries;
    };

    inline EventListenerState::EventListenerState()
        : ReadinessState()
        , epoll(-1)
        , wake(-1)
        , entries()
    {
    }
}

#endif
