#pragma once

#if defined(__linux__)

#include "data/infra/event/kiotty_event_listener.h"

#include <sys/epoll.h>

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct EventListenerState
    {
        int           epoll;
        int           wake;
        epoll_event*  entries;
        IoCompletion* completions;
        size_t        capacity;
        uint32_t      generation;
        IoEvent*      ready_head;
        IoEvent*      ready_tail;
    };

    static_assert(sizeof(EventListenerState) <= EventListener::STORAGE_SIZE,
        "EventListenerState outgrew EventListener::STORAGE_SIZE");
    static_assert(alignof(EventListenerState) <= EventListener::STORAGE_ALIGN,
        "EventListenerState needs stricter alignment than EventListener::STORAGE_ALIGN");

    namespace backend
    {
        EventListenerState& stateOf(unsigned char* storage);
        bool                attached(const IoEvent& event);
        bool                belongsTo(const IoEvent& event, const EventListenerState& state);
        void                disarm(IoEvent& event);
        void                queueReady(EventListenerState& state, IoEvent& event, SocketCode code);
        SocketCode          watch(EventListenerState& state, IoEvent& event);
        SocketCode          sendRest(IoEvent& event);
    }

    inline EventListenerState& backend::stateOf(unsigned char* storage)
    {
        return *static_cast<EventListenerState*>(static_cast<void*>(storage));
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
