#pragma once

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_READINESS

#include "data/infra/event/kiotty_event_listener.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    namespace backend
    {
        using Clock = std::chrono::steady_clock;

        bool       attached(const IoEvent& event);
        bool       belongsTo(const IoEvent& event, const EventListenerState& state);
        void       disarm(IoEvent& event);
        bool       isForever(uint32_t timeout_milliseconds);

        bool       isOpen(const EventListenerState& state);
        SocketCode createHandles(EventListenerState& state, size_t completion_capacity);
        void       releaseHandles(EventListenerState& state);
        SocketCode watch(EventListenerState& state, IoEvent& event);
        void       unwatch(EventListenerState& state, IoEvent& event);
        size_t     poll(EventListenerState& state, uint32_t timeout_milliseconds);

        void         queueReady(EventListenerState& state, IoEvent& event, SocketCode code);
        void         unlinkReady(EventListenerState& state, IoEvent& event);
        void         cancelReady(EventListenerState& state);
        size_t       drainReady(EventListenerState& state);
        IoCompletion completionOf(IoEvent& event, SocketCode code);

        SocketCode sendRest(IoEvent& event);
        SocketCode checkRequest(const IoAction& action, IoPayload payload);
        size_t     settle(EventListenerState& state, IoEvent& event, size_t count,
                          SocketCode eof_error);
        size_t     noteWake(EventListenerState& state, size_t count);
        int        remainingMilliseconds(Clock::time_point deadline);
    }

    inline bool backend::attached(const IoEvent& event)
    {
        return event.fd >= 0;
    }

    inline bool backend::belongsTo(const IoEvent& event, const EventListenerState& state)
    {
        const bool is_same_listener = event.listener == &state;
        const bool is_same_generation = event.generation == state.generation;
        return is_same_listener && is_same_generation && isOpen(state);
    }

    inline void backend::disarm(IoEvent& event)
    {
        event.pending = false;
    }

    inline bool backend::isForever(uint32_t timeout_milliseconds)
    {
        return timeout_milliseconds == EventListener::WAIT_FOREVER;
    }
}

#endif
