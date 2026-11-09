#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_READINESS

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <fcntl.h>
#include <unistd.h>

#include <climits>

namespace kiotty::net::implementation
{
    using backend::attached;
    using backend::belongsTo;
    using backend::queueReady;

    namespace
    {
        bool       isUsableCapacity(size_t completion_capacity);
        SocketCode checkAttach(const EventListenerState& state, const IoEvent& event,
            SocketHandle socket);
    }

    EventListener::EventListener()
        : _state()
    {
    }

    EventListener::~EventListener()
    {
        close();
    }

    SocketCode EventListener::open(size_t completion_capacity)
    {
        EventListenerState& state = _state;
        if (backend::isOpen(state)) { return SocketCode::INVALID_ARGUMENT; }
        if (!isUsableCapacity(completion_capacity)) { return SocketCode::INVALID_ARGUMENT; }
        state.completions.resize(completion_capacity);
        const SocketCode created = backend::createHandles(state, completion_capacity);
        if (created != SocketCode::SUCCESS)
        {
            close();
            return created;
        }
        state.capacity = completion_capacity;
        state.generation += 1;
        return SocketCode::SUCCESS;
    }

    void EventListener::close()
    {
        EventListenerState& state = _state;
        backend::cancelReady(state);
        backend::releaseHandles(state);
        state.completions.clear();
        state.capacity = 0;
    }

    SocketCode EventListener::attach(IoEvent& event, SocketHandle socket, int32_t slot)
    {
        EventListenerState& state = _state;
        const SocketCode checked = checkAttach(state, event, socket);
        if (checked != SocketCode::SUCCESS) { return checked; }
        const int copy = ::fcntl(static_cast<int>(socket), F_DUPFD_CLOEXEC, 0);
        if (copy < 0) { return lastSocketCode(); }
        event.listener = &state;
        event.generation = state.generation;
        event.fd = copy;
        event.slot = slot;
        event.registered = false;
        event.result = SocketCode::SUCCESS;
        event.done = 0;
        return SocketCode::SUCCESS;
    }

    void EventListener::detach(IoEvent& event)
    {
        EventListenerState& state = _state;
        if (!attached(event) || event.listener != &state)
        {
            return;
        }
        const bool is_current = belongsTo(event, state);
        if (is_current && event.registered)
        {
            backend::unwatch(state, event);
        }
        if (is_current && event.pending)
        {
            queueReady(state, event, SocketCode::CANCELLED);
        }
        event.registered = false;
        ::close(event.fd);
        event.fd = -1;
    }

    namespace
    {
        bool isUsableCapacity(size_t completion_capacity)
        {
            return completion_capacity > 0 && completion_capacity <= static_cast<size_t>(INT_MAX);
        }

        SocketCode checkAttach(const EventListenerState& state, const IoEvent& event,
            SocketHandle socket)
        {
            if (!backend::isOpen(state)) { return SocketCode::CLOSED; }
            if (socket == INVALID_SOCKET_HANDLE) { return SocketCode::INVALID_ARGUMENT; }
            if (event.pending) { return SocketCode::BUSY; }
            if (attached(event)) { return SocketCode::INVALID_ARGUMENT; }
            return SocketCode::SUCCESS;
        }
    }
}

#endif
