#include "data/infra/event/backend/kiotty_event_backend.h"

#if !KIOTTY_NET_HAS_EVENT_BACKEND

#include "data/infra/event/kiotty_event_listener.h"

namespace kiotty::net::implementation
{
    EventListener::EventListener()
        : _state()
    {
    }

    EventListener::~EventListener()
    {
        (void)_state;
    }

    SocketCode EventListener::openBackend(size_t completion_capacity)
    {
        (void)completion_capacity;
        return SocketCode::NOT_SUPPORTED;
    }

    void EventListener::closeBackend()
    {
    }

    SocketCode EventListener::attach(IoEvent& event, SocketHandle socket, int32_t slot)
    {
        (void)event;
        (void)socket;
        (void)slot;
        return SocketCode::NOT_SUPPORTED;
    }

    SocketCode EventListener::submit(IoEvent& event, const IoAction& action, IoPayload payload)
    {
        (void)event;
        (void)action;
        (void)payload;
        return SocketCode::NOT_SUPPORTED;
    }

    void EventListener::detach(IoEvent& event)
    {
        event.pending = false;
    }

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_milliseconds)
    {
        (void)timeout_milliseconds;
        Span<const IoCompletion> completions = { nullptr, 0 };
        return completions;
    }

    SocketCode EventListener::wakeBackend()
    {
        return SocketCode::NOT_SUPPORTED;
    }
}

#endif
