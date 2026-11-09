#pragma once

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_EPOLL

#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    static constexpr bool RING_BACKEND = KIOTTY_NET_EVENT_URING != 0;

    namespace backend
    {
        bool       isRing(const EventListenerState& state);
        SocketCode ringOpen(EventListenerState& state, size_t completion_capacity);
        void       ringClose(EventListenerState& state);
        SocketCode ringWatch(EventListenerState& state, IoEvent& event);
        void       ringCancel(EventListenerState& state, IoEvent& event);
        void       ringForget(EventListenerState& state, IoEvent& event);
        size_t     ringPoll(EventListenerState& state, uint32_t timeout_milliseconds);
    }

    inline bool backend::isRing(const EventListenerState& state)
    {
        return state.ring.fd >= 0;
    }
}

#endif
