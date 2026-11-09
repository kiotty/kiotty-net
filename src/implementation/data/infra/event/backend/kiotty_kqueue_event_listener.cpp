#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_KQUEUE

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

namespace kiotty::net::implementation
{
    namespace
    {
        const uintptr_t WAKE_IDENT = 1;

        int16_t    filterOf(const IoAction& action);
        SocketCode change(const EventListenerState& state, const struct kevent& request);
    }

    SocketCode EventListener::wakeBackend()
    {
        EventListenerState& state = _state;
        if (!backend::isOpen(state))
        {
            return SocketCode::CLOSED;
        }
        struct kevent request;
        EV_SET(&request, WAKE_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        return change(state, request);
    }

    bool backend::isOpen(const EventListenerState& state)
    {
        return state.queue >= 0;
    }

    SocketCode backend::createHandles(EventListenerState& state, size_t completion_capacity)
    {
        state.entries.resize(completion_capacity);
        state.queue = ::kqueue();
        if (state.queue < 0) { return lastSocketCode(); }
        const int flagged = ::fcntl(state.queue, F_SETFD, FD_CLOEXEC);
        if (flagged < 0) { return lastSocketCode(); }
        struct kevent request;
        EV_SET(&request, WAKE_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        return change(state, request);
    }

    void backend::releaseHandles(EventListenerState& state)
    {
        if (state.queue >= 0)
        {
            ::close(state.queue);
        }
        state.entries.clear();
        state.queue = -1;
    }

    SocketCode backend::watch(EventListenerState& state, IoEvent& event)
    {
        struct kevent request;
        const uintptr_t ident = static_cast<uintptr_t>(event.fd);
        EV_SET(&request, ident, filterOf(event.action), EV_ADD | EV_ENABLE | EV_ONESHOT,
            0, 0, &event);
        const SocketCode changed = change(state, request);
        if (changed != SocketCode::SUCCESS)
        {
            return changed;
        }
        event.registered = true;
        return SocketCode::SUCCESS;
    }

    void backend::unwatch(EventListenerState& state, IoEvent& event)
    {
        struct kevent request;
        const uintptr_t ident = static_cast<uintptr_t>(event.fd);
        EV_SET(&request, ident, filterOf(event.action), EV_DELETE, 0, 0, nullptr);
        static_cast<void>(change(state, request));
        event.registered = false;
    }

    void backend::forget(EventListenerState& state, IoEvent& event)
    {
        unwatch(state, event);
    }

    namespace
    {
        int16_t filterOf(const IoAction& action)
        {
            if (action.kind == IoActionKind::WRITE)
            {
                return EVFILT_WRITE;
            }
            return EVFILT_READ;
        }

        SocketCode change(const EventListenerState& state, const struct kevent& request)
        {
            const int changed = ::kevent(state.queue, &request, 1, nullptr, 0, nullptr);
            if (changed < 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }
    }
}

#endif
