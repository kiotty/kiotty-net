#if !defined(_WIN32) && !defined(__linux__)

#include "base/event/kiotty_event_listener.h"

namespace kiotty::net::implementation
{
    EventListener::EventListener()
        : _storage()
    {
    }

    EventListener::~EventListener()
    {
        (void)_storage;
    }

    SocketCode EventListener::open(size_t completion_capacity)
    {
        (void)completion_capacity;
        return SocketCode::NOT_SUPPORTED;
    }

    void EventListener::close()
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

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_ms)
    {
        (void)timeout_ms;
        Span<const IoCompletion> completions = { nullptr, 0 };
        return completions;
    }

    SocketCode EventListener::wake()
    {
        return SocketCode::NOT_SUPPORTED;
    }
}

#endif
