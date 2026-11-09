#pragma once

#if defined(__linux__)

#include "data/infra/event/kiotty_event_listener.h"

#include <sys/epoll.h>

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    namespace backend
    {
        bool       attached(const IoEvent& event);
        bool       belongsTo(const IoEvent& event, const EventListenerState& state);
        void       disarm(IoEvent& event);
        void       queueReady(EventListenerState& state, IoEvent& event, SocketCode code);
        SocketCode watch(EventListenerState& state, IoEvent& event);
        SocketCode sendRest(IoEvent& event);
    }

    inline bool backend::attached(const IoEvent& event)
    {
        return event.fd >= 0;
    }

    inline bool backend::belongsTo(const IoEvent& event, const EventListenerState& state)
    {
        const bool is_same_listener = event.listener == &state;
        const bool is_same_generation = event.generation == state.generation;
        return is_same_listener && is_same_generation && state.epoll >= 0;
    }

    inline void backend::disarm(IoEvent& event)
    {
        event.pending = false;
    }
}

#endif
