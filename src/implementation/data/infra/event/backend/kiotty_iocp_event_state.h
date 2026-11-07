#pragma once

#if defined(_WIN32)

#include "data/infra/event/kiotty_event_listener.h"

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct EventListenerState
    {
        HANDLE            port;
        OVERLAPPED_ENTRY* entries;
        IoCompletion*     completions;
        size_t            capacity;
        uint32_t          generation;
    };

    static_assert(sizeof(EventListenerState) <= EventListener::STORAGE_SIZE,
        "EventListenerState outgrew EventListener::STORAGE_SIZE");
    static_assert(alignof(EventListenerState) <= EventListener::STORAGE_ALIGN,
        "EventListenerState needs stricter alignment than EventListener::STORAGE_ALIGN");
    static_assert(sizeof(sockaddr_storage) == AcceptBuffer::SOCKET_ADDRESS_BYTES,
        "AcceptEx needs sizeof(sockaddr_storage) plus padding per address");

    namespace backend
    {
        EventListenerState& stateOf(unsigned char* storage);
        bool                attached(const IoEvent& event);
        bool                belongsTo(const IoEvent& event, const EventListenerState& state);
        void                disarm(IoEvent& event);
    }

    inline EventListenerState& backend::stateOf(unsigned char* storage)
    {
        return *static_cast<EventListenerState*>(static_cast<void*>(storage));
    }

    inline bool backend::attached(const IoEvent& event)
    {
        return event.socket != INVALID_SOCKET;
    }

    inline bool backend::belongsTo(const IoEvent& event, const EventListenerState& state)
    {
        return event.listener == &state && event.generation == state.generation;
    }

    inline void backend::disarm(IoEvent& event)
    {
        event.pending = false;
    }
}

#endif
