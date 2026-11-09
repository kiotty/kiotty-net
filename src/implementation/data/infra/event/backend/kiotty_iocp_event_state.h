#pragma once

#if defined(_WIN32)

#include "data/infra/event/kiotty_event_listener.h"

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    static_assert(sizeof(sockaddr_storage) == AcceptBuffer::SOCKET_ADDRESS_BYTES,
        "AcceptEx needs sizeof(sockaddr_storage) plus padding per address");

    namespace backend
    {
        bool       attached(const IoEvent& event);
        bool       belongsTo(const IoEvent& event, const EventListenerState& state);
        void       disarm(IoEvent& event);
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
