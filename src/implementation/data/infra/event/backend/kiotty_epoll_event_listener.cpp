#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_EPOLL

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>

namespace kiotty::net::implementation
{
    namespace
    {
        uint32_t interestOf(const IoAction& action);
    }

    SocketCode EventListener::wake()
    {
        EventListenerState& state = _state;
        if (!backend::isOpen(state))
        {
            return SocketCode::CLOSED;
        }
        const uint64_t one = 1;
        const ssize_t written = ::write(state.wake, &one, sizeof(one));
        if (written < 0 && errno != EAGAIN)
        {
            return lastSocketCode();
        }
        return SocketCode::SUCCESS;
    }

    bool backend::isOpen(const EventListenerState& state)
    {
        return state.epoll >= 0;
    }

    SocketCode backend::createHandles(EventListenerState& state, size_t completion_capacity)
    {
        state.entries.resize(completion_capacity);
        state.epoll = ::epoll_create1(EPOLL_CLOEXEC);
        if (state.epoll < 0) { return lastSocketCode(); }
        state.wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (state.wake < 0) { return lastSocketCode(); }
        epoll_event interest{};
        interest.events = EPOLLIN;
        interest.data.ptr = nullptr;
        const int added = ::epoll_ctl(state.epoll, EPOLL_CTL_ADD, state.wake, &interest);
        if (added != 0) { return lastSocketCode(); }
        return SocketCode::SUCCESS;
    }

    void backend::releaseHandles(EventListenerState& state)
    {
        if (state.wake >= 0)
        {
            ::close(state.wake);
        }
        if (state.epoll >= 0)
        {
            ::close(state.epoll);
        }
        state.entries.clear();
        state.wake = -1;
        state.epoll = -1;
    }

    SocketCode backend::watch(EventListenerState& state, IoEvent& event)
    {
        epoll_event interest{};
        interest.events = interestOf(event.action);
        interest.data.ptr = &event;
        int operation = EPOLL_CTL_ADD;
        if (event.registered)
        {
            operation = EPOLL_CTL_MOD;
        }
        const int controlled = ::epoll_ctl(state.epoll, operation, event.fd, &interest);
        if (controlled != 0)
        {
            return lastSocketCode();
        }
        event.registered = true;
        return SocketCode::SUCCESS;
    }

    void backend::unwatch(EventListenerState& state, IoEvent& event)
    {
        ::epoll_ctl(state.epoll, EPOLL_CTL_DEL, event.fd, nullptr);
        event.registered = false;
    }

    namespace
    {
        uint32_t interestOf(const IoAction& action)
        {
            if (action.kind == IoActionKind::WRITE)
            {
                return EPOLLOUT | EPOLLONESHOT;
            }
            return EPOLLIN | EPOLLONESHOT;
        }
    }
}

#endif
