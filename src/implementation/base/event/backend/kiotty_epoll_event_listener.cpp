#if defined(__linux__)

#include "base/event/kiotty_event_listener.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <new>

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

    namespace
    {
        static_assert(sizeof(EventListenerState) <= EventListener::STORAGE_SIZE, "EventListenerState outgrew EventListener::STORAGE_SIZE");
        static_assert(alignof(EventListenerState) <= EventListener::STORAGE_ALIGN, "EventListenerState needs stricter alignment than EventListener::STORAGE_ALIGN");

        EventListenerState& stateOf(void* storage)
        {
            return *static_cast<EventListenerState*>(storage);
        }

        void arm(IoEvent& event, const IoAction& action, IoPayload request)
        {
            event.pending = true;
            event.action = action;
            event.payload = request;
            event.done = 0;
            event.result = SocketCode::SUCCESS;
        }

        void disarm(IoEvent& event)
        {
            event.pending = false;
        }

        bool attached(const IoEvent& event)
        {
            return event.fd >= 0;
        }

        bool belongsTo(const IoEvent& event, const EventListenerState& state)
        {
            return event.listener == &state && event.generation == state.generation && state.epoll >= 0;
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

        void queueReady(EventListenerState& state, IoEvent& event, SocketCode code)
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

        SocketCode checkRequest(const IoAction& action, IoPayload payload)
        {
            switch (action.kind)
            {
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::WRITE:
                if (payload.buffer == nullptr || payload.length == 0)
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                return SocketCode::SUCCESS;
            case IoActionKind::ACCEPT:
                return SocketCode::SUCCESS;
            default:
                return SocketCode::INVALID_ARGUMENT;
            }
        }

        int sendFlags()
        {
#if defined(MSG_NOSIGNAL)
            return MSG_NOSIGNAL;
#else
            return 0;
#endif
        }

        uint32_t interestOf(const IoAction& action)
        {
            if (action.kind == IoActionKind::WRITE)
            {
                return EPOLLOUT | EPOLLONESHOT;
            }
            return EPOLLIN | EPOLLONESHOT;
        }

        SocketCode watch(EventListenerState& state, IoEvent& event)
        {
            epoll_event interest;
            std::memset(&interest, 0, sizeof(interest));
            interest.events = interestOf(event.action);
            interest.data.ptr = &event;
            int operation = EPOLL_CTL_ADD;
            if (event.registered)
            {
                operation = EPOLL_CTL_MOD;
            }
            if (::epoll_ctl(state.epoll, operation, event.fd, &interest) != 0)
            {
                return lastSocketCode();
            }
            event.registered = true;
            return SocketCode::SUCCESS;
        }

        SocketCode sendRest(IoEvent& event)
        {
            const char* bytes = static_cast<const char*>(event.payload.buffer);
            while (event.done < event.payload.length)
            {
                size_t rest = event.payload.length - event.done;
                ssize_t sent = ::send(event.fd, bytes + event.done, rest, sendFlags());
                if (sent > 0)
                {
                    event.done += static_cast<size_t>(sent);
                    continue;
                }
                if (sent < 0 && errno == EINTR)
                {
                    continue;
                }
                if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    return SocketCode::WOULD_BLOCK;
                }
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode receiveOnce(IoEvent& event)
        {
            for (;;)
            {
                ssize_t received = ::recv(event.fd, event.payload.buffer, event.payload.length, 0);
                if (received > 0)
                {
                    event.done = static_cast<size_t>(received);
                    return SocketCode::SUCCESS;
                }
                if (received == 0)
                {
                    return SocketCode::CLOSED;
                }
                if (errno == EINTR)
                {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    return SocketCode::WOULD_BLOCK;
                }
                return lastSocketCode();
            }
        }

        SocketCode progress(IoEvent& event)
        {
            switch (event.action.kind)
            {
            case IoActionKind::WRITE:
                return sendRest(event);
            case IoActionKind::ACCEPT:
                return SocketCode::SUCCESS;
            default:
                return receiveOnce(event);
            }
        }

        IoKind kindOf(const IoAction& action)
        {
            switch (action.kind)
            {
            case IoActionKind::ACCEPT:
                return IoKind::ACCEPT;
            case IoActionKind::WRITE:
                return IoKind::SEND;
            default:
                return IoKind::RECEIVE;
            }
        }

        IoCompletion completionOf(IoEvent& event, SocketCode code)
        {
            IoCompletion completion;
            completion.kind = kindOf(event.action);
            completion.slot = event.slot;
            completion.code = code;
            completion.length = event.done;
            completion.action = event.action;
            disarm(event);
            return completion;
        }

        size_t drainReady(EventListenerState& state)
        {
            size_t count = 0;
            while (state.ready_head != nullptr && count < state.capacity)
            {
                IoEvent& event = *state.ready_head;
                state.ready_head = event.next;
                if (state.ready_head == nullptr)
                {
                    state.ready_tail = nullptr;
                }
                event.next = nullptr;
                event.queued = false;
                state.completions[count] = completionOf(event, event.result);
                count += 1;
            }
            return count;
        }

        void clearWake(EventListenerState& state)
        {
            uint64_t counter = 0;
            ssize_t consumed = ::read(state.wake, &counter, sizeof(counter));
            (void)consumed;
        }

        bool handle(EventListenerState& state, IoEvent& event, IoCompletion& completion)
        {
            if (!event.pending || event.queued)
            {
                return false;
            }
            SocketCode advanced = progress(event);
            if (advanced == SocketCode::WOULD_BLOCK)
            {
                SocketCode watched = watch(state, event);
                if (watched == SocketCode::SUCCESS)
                {
                    return false;
                }
                advanced = watched;
            }
            completion = completionOf(event, advanced);
            return true;
        }

        int remainingMilliseconds(std::chrono::steady_clock::time_point deadline)
        {
            std::chrono::steady_clock::duration left = deadline - std::chrono::steady_clock::now();
            if (left <= std::chrono::steady_clock::duration::zero())
            {
                return 0;
            }
            std::chrono::milliseconds whole = std::chrono::duration_cast<std::chrono::milliseconds>(left);
            if (whole < left)
            {
                whole += std::chrono::milliseconds(1);
            }
            if (whole.count() > INT_MAX)
            {
                return INT_MAX;
            }
            return static_cast<int>(whole.count());
        }

        size_t poll(EventListenerState& state, size_t room, uint32_t timeout_milliseconds)
        {
            std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_milliseconds);
            int limit = -1;
            if (timeout_milliseconds != EventListener::WAIT_FOREVER)
            {
                limit = remainingMilliseconds(deadline);
            }
            for (;;)
            {
                int ready = ::epoll_wait(state.epoll, state.entries, static_cast<int>(room), limit);
                if (ready < 0 && errno != EINTR)
                {
                    return 0;
                }
                size_t count = 0;
                for (int i = 0; i < ready; ++i)
                {
                    void* tag = state.entries[i].data.ptr;
                    if (tag == nullptr)
                    {
                        clearWake(state);
                        state.completions[count] = IoCompletion::wake();
                        count += 1;
                        continue;
                    }
                    IoEvent& event = *static_cast<IoEvent*>(tag);
                    bool completed = handle(state, event, state.completions[count]);
                    if (completed)
                    {
                        count += 1;
                    }
                }
                if (count > 0)
                {
                    return count;
                }
                if (timeout_milliseconds == EventListener::WAIT_FOREVER)
                {
                    continue;
                }
                limit = remainingMilliseconds(deadline);
                if (limit == 0)
                {
                    return 0;
                }
            }
        }
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
    {
        EventListenerState* state = ::new (static_cast<void*>(_storage)) EventListenerState();
        state->epoll = -1;
        state->wake = -1;
    }

    EventListener::~EventListener()
    {
        close();
    }

    SocketCode EventListener::open(size_t completion_capacity)
    {
        EventListenerState& state = stateOf(_storage);
        if (state.epoll >= 0)
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        if (completion_capacity == 0 || completion_capacity > static_cast<size_t>(INT_MAX))
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        state.entries = new (std::nothrow) epoll_event[completion_capacity];
        state.completions = new (std::nothrow) IoCompletion[completion_capacity];
        if (state.entries == nullptr || state.completions == nullptr)
        {
            close();
            return SocketCode::OUT_OF_RESOURCE;
        }
        state.epoll = ::epoll_create1(EPOLL_CLOEXEC);
        if (state.epoll < 0)
        {
            SocketCode failed = lastSocketCode();
            close();
            return failed;
        }
        state.wake = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (state.wake < 0)
        {
            SocketCode failed = lastSocketCode();
            close();
            return failed;
        }
        epoll_event interest;
        std::memset(&interest, 0, sizeof(interest));
        interest.events = EPOLLIN;
        interest.data.ptr = nullptr;
        if (::epoll_ctl(state.epoll, EPOLL_CTL_ADD, state.wake, &interest) != 0)
        {
            SocketCode failed = lastSocketCode();
            close();
            return failed;
        }
        state.capacity = completion_capacity;
        state.generation += 1;
        return SocketCode::SUCCESS;
    }

    void EventListener::close()
    {
        EventListenerState& state = stateOf(_storage);
        while (state.ready_head != nullptr)
        {
            IoEvent* event = state.ready_head;
            state.ready_head = event->next;
            event->next = nullptr;
            event->queued = false;
            disarm(*event);
        }
        state.ready_tail = nullptr;
        if (state.wake >= 0)
        {
            ::close(state.wake);
        }
        if (state.epoll >= 0)
        {
            ::close(state.epoll);
        }
        delete[] state.entries;
        delete[] state.completions;
        state.wake = -1;
        state.epoll = -1;
        state.entries = nullptr;
        state.completions = nullptr;
        state.capacity = 0;
    }

    SocketCode EventListener::attach(IoEvent& event, SocketHandle socket, int32_t slot)
    {
        EventListenerState& state = stateOf(_storage);
        if (state.epoll < 0)
        {
            return SocketCode::CLOSED;
        }
        if (socket == INVALID_SOCKET_HANDLE)
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        if (event.pending)
        {
            return SocketCode::BUSY;
        }
        if (attached(event))
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        int copy = ::fcntl(static_cast<int>(socket), F_DUPFD_CLOEXEC, 0);
        if (copy < 0)
        {
            return lastSocketCode();
        }
        event.listener = &state;
        event.generation = state.generation;
        event.fd = copy;
        event.slot = slot;
        event.registered = false;
        event.result = SocketCode::SUCCESS;
        event.done = 0;
        return SocketCode::SUCCESS;
    }

    SocketCode EventListener::submit(IoEvent& event, const IoAction& action, IoPayload payload)
    {
        EventListenerState& state = stateOf(_storage);
        if (state.epoll < 0)
        {
            return SocketCode::CLOSED;
        }
        if (!attached(event) || !belongsTo(event, state))
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        if (event.pending)
        {
            return SocketCode::BUSY;
        }
        SocketCode checked = checkRequest(action, payload);
        if (checked != SocketCode::SUCCESS)
        {
            return checked;
        }
        arm(event, action, payload);
        SocketCode started = SocketCode::WOULD_BLOCK;
        if (action.kind == IoActionKind::WRITE)
        {
            started = sendRest(event);
        }
        if (started == SocketCode::WOULD_BLOCK)
        {
            started = watch(state, event);
            if (started == SocketCode::SUCCESS)
            {
                return SocketCode::SUCCESS;
            }
        }
        queueReady(state, event, started);
        return SocketCode::SUCCESS;
    }

    void EventListener::detach(IoEvent& event)
    {
        EventListenerState& state = stateOf(_storage);
        if (!attached(event) || event.listener != &state)
        {
            return;
        }
        if (belongsTo(event, state))
        {
            if (event.registered)
            {
                ::epoll_ctl(state.epoll, EPOLL_CTL_DEL, event.fd, nullptr);
            }
            if (event.pending)
            {
                queueReady(state, event, SocketCode::CANCELLED);
            }
        }
        event.registered = false;
        ::close(event.fd);
        event.fd = -1;
    }

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_milliseconds)
    {
        EventListenerState& state = stateOf(_storage);
        Span<const IoCompletion> completions = { state.completions, 0 };
        if (state.epoll < 0)
        {
            return completions;
        }
        size_t ready = drainReady(state);
        if (ready > 0)
        {
            completions.length = ready;
            return completions;
        }
        completions.length = poll(state, state.capacity, timeout_milliseconds);
        return completions;
    }

    SocketCode EventListener::wake()
    {
        EventListenerState& state = stateOf(_storage);
        if (state.epoll < 0)
        {
            return SocketCode::CLOSED;
        }
        uint64_t one = 1;
        ssize_t written = ::write(state.wake, &one, sizeof(one));
        if (written < 0 && errno != EAGAIN)
        {
            return lastSocketCode();
        }
        return SocketCode::SUCCESS;
    }
}

#endif
