#if defined(__linux__)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_epoll_event_state.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <climits>

namespace kiotty::net::implementation
{
    using backend::attached;
    using backend::belongsTo;
    using backend::disarm;
    using backend::queueReady;

    namespace
    {
        bool       isUsableCapacity(size_t completion_capacity);
        SocketCode allocate(EventListenerState& state, size_t completion_capacity);
        SocketCode createHandles(EventListenerState& state);
        SocketCode checkAttach(const EventListenerState& state, const IoEvent& event,
            SocketHandle socket);
        void       cancelReady(EventListenerState& state);
        void       releaseHandles(EventListenerState& state);
        void       unlinkReady(EventListenerState& state, IoEvent& event);
    }

    IoEvent::~IoEvent()
    {
        if (queued)
        {
            unlinkReady(*listener, *this);
        }
        if (!attached(*this))
        {
            return;
        }
        if (registered && belongsTo(*this, *listener))
        {
            ::epoll_ctl(listener->epoll, EPOLL_CTL_DEL, fd, nullptr);
        }
        ::close(fd);
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
        if (state.epoll >= 0) { return SocketCode::INVALID_ARGUMENT; }
        if (!isUsableCapacity(completion_capacity)) { return SocketCode::INVALID_ARGUMENT; }
        SocketCode code = allocate(state, completion_capacity);
        if (code == SocketCode::SUCCESS) { code = createHandles(state); }
        if (code != SocketCode::SUCCESS)
        {
            close();
            return code;
        }
        state.capacity = completion_capacity;
        state.generation += 1;
        return SocketCode::SUCCESS;
    }

    void EventListener::close()
    {
        EventListenerState& state = _state;
        cancelReady(state);
        releaseHandles(state);
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
            ::epoll_ctl(state.epoll, EPOLL_CTL_DEL, event.fd, nullptr);
        }
        if (is_current && event.pending)
        {
            queueReady(state, event, SocketCode::CANCELLED);
        }
        event.registered = false;
        ::close(event.fd);
        event.fd = -1;
    }

    SocketCode EventListener::wake()
    {
        EventListenerState& state = _state;
        if (state.epoll < 0)
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

    namespace
    {
        bool isUsableCapacity(size_t completion_capacity)
        {
            return completion_capacity > 0 && completion_capacity <= static_cast<size_t>(INT_MAX);
        }

        SocketCode allocate(EventListenerState& state, size_t completion_capacity)
        {
            state.entries.resize(completion_capacity);
            state.completions.resize(completion_capacity);
            return SocketCode::SUCCESS;
        }

        SocketCode createHandles(EventListenerState& state)
        {
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

        SocketCode checkAttach(const EventListenerState& state, const IoEvent& event,
            SocketHandle socket)
        {
            if (state.epoll < 0) { return SocketCode::CLOSED; }
            if (socket == INVALID_SOCKET_HANDLE) { return SocketCode::INVALID_ARGUMENT; }
            if (event.pending) { return SocketCode::BUSY; }
            if (attached(event)) { return SocketCode::INVALID_ARGUMENT; }
            return SocketCode::SUCCESS;
        }

        void cancelReady(EventListenerState& state)
        {
            while (state.ready_head != nullptr)
            {
                IoEvent* event = state.ready_head;
                state.ready_head = event->next;
                event->next = nullptr;
                event->queued = false;
                disarm(*event);
            }
            state.ready_tail = nullptr;
        }

        void releaseHandles(EventListenerState& state)
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
            state.completions.clear();
            state.wake = -1;
            state.epoll = -1;
            state.capacity = 0;
        }

        void unlinkReady(EventListenerState& state, IoEvent& event)
        {
            IoEvent* previous = nullptr;
            IoEvent* current = state.ready_head;
            while (current != nullptr && current != &event)
            {
                previous = current;
                current = current->next;
            }
            if (current == nullptr)
            {
                return;
            }
            if (previous == nullptr)
            {
                state.ready_head = event.next;
            }
            else
            {
                previous->next = event.next;
            }
            if (state.ready_tail == &event)
            {
                state.ready_tail = previous;
            }
            event.next = nullptr;
            event.queued = false;
        }
    }

    void backend::queueReady(EventListenerState& state, IoEvent& event, SocketCode code)
    {
        if (event.queued)
        {
            return;
        }
        event.result = code;
        event.queued = true;
        event.next = nullptr;
        if (state.ready_tail == nullptr)
        {
            state.ready_head = &event;
        }
        else
        {
            state.ready_tail->next = &event;
        }
        state.ready_tail = &event;
    }
}

#endif
