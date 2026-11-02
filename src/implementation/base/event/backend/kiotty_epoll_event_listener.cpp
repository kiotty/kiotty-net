#include "base/event/kiotty_event_listener.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <new>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            class EpollEvent;

            struct ListenerState
            {
                int           epoll;
                int           wake;
                epoll_event*  entries;
                IoCompletion* completions;
                size_t        capacity;
                uint32_t      generation;
                EpollEvent*   ready_head;
                EpollEvent*   ready_tail;
            };

            static_assert(sizeof(ListenerState) <= EventListener::STORAGE_SIZE, "ListenerState outgrew EventListener::STORAGE_SIZE");
            static_assert(alignof(ListenerState) <= EventListener::STORAGE_ALIGN, "ListenerState needs stricter alignment than EventListener::STORAGE_ALIGN");

            void unlinkReady(ListenerState& state, EpollEvent& event);

            class EpollEvent : public IoEvent
            {
            public:
                EpollEvent(ListenerState* owner, int descriptor, int32_t index)
                    : listener(owner)
                    , generation(owner->generation)
                    , fd(descriptor)
                    , slot(index)
                    , registered(false)
                    , queued(false)
                    , result(SocketCode::SUCCESS)
                    , payload()
                    , done(0)
                    , next(nullptr)
                {
                }

                EpollEvent(EpollEvent&& other) noexcept
                    : IoEvent(other)
                    , listener(other.listener)
                    , generation(other.generation)
                    , fd(other.fd)
                    , slot(other.slot)
                    , registered(other.registered)
                    , queued(false)
                    , result(other.result)
                    , payload(other.payload)
                    , done(other.done)
                    , next(nullptr)
                {
                    assert(!other._pending && "an IoEvent must not move while it is registered for a request");
                    assert(!other.queued && "an IoEvent must not move while its completion is queued");
                    other.fd = -1;
                    other.registered = false;
                }

                ~EpollEvent()
                {
                    if (queued)
                    {
                        unlinkReady(*listener, *this);
                    }
                    if (fd < 0)
                    {
                        return;
                    }
                    if (registered && belongsTo(*listener))
                    {
                        ::epoll_ctl(listener->epoll, EPOLL_CTL_DEL, fd, nullptr);
                    }
                    ::close(fd);
                }

                void arm(const IoAction& action, IoPayload request)
                {
                    _pending = true;
                    _action = action;
                    payload = request;
                    done = 0;
                    result = SocketCode::SUCCESS;
                }

                void disarm()
                {
                    _pending = false;
                }

                bool belongsTo(const ListenerState& state) const
                {
                    return listener == &state && generation == state.generation && state.epoll >= 0;
                }

                ListenerState* listener;
                uint32_t       generation;
                int            fd;
                int32_t        slot;
                bool           registered;
                bool           queued;
                SocketCode     result;
                IoPayload      payload;
                size_t         done;
                EpollEvent*    next;
            };

            ListenerState& stateOf(void* storage)
            {
                return *static_cast<ListenerState*>(storage);
            }

            void unlinkReady(ListenerState& state, EpollEvent& event)
            {
                EpollEvent* previous = nullptr;
                EpollEvent* current = state.ready_head;
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

            void queueReady(ListenerState& state, EpollEvent& event, SocketCode code)
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

            SocketCode watch(ListenerState& state, EpollEvent& event)
            {
                epoll_event interest;
                std::memset(&interest, 0, sizeof(interest));
                interest.events = interestOf(event.action());
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

            SocketCode sendRest(EpollEvent& event)
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

            SocketCode receiveOnce(EpollEvent& event)
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

            SocketCode progress(EpollEvent& event)
            {
                switch (event.action().kind)
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

            IoCompletion completionOf(EpollEvent& event, SocketCode code)
            {
                IoCompletion completion;
                completion.kind = kindOf(event.action());
                completion.slot = event.slot;
                completion.code = code;
                completion.length = event.done;
                completion.action = event.action();
                event.disarm();
                return completion;
            }

            size_t drainReady(ListenerState& state)
            {
                size_t count = 0;
                while (state.ready_head != nullptr && count < state.capacity)
                {
                    EpollEvent& event = *state.ready_head;
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

            void clearWake(ListenerState& state)
            {
                uint64_t counter = 0;
                ssize_t consumed = ::read(state.wake, &counter, sizeof(counter));
                (void)consumed;
            }

            bool handle(ListenerState& state, EpollEvent& event, IoCompletion& completion)
            {
                if (!event.pending() || event.queued)
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

            int remainingMs(std::chrono::steady_clock::time_point deadline)
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

            size_t poll(ListenerState& state, size_t room, uint32_t timeout_ms)
            {
                std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
                int limit = -1;
                if (timeout_ms != EventListener::WAIT_FOREVER)
                {
                    limit = remainingMs(deadline);
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
                        EpollEvent& event = *static_cast<EpollEvent*>(tag);
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
                    if (timeout_ms == EventListener::WAIT_FOREVER)
                    {
                        continue;
                    }
                    limit = remainingMs(deadline);
                    if (limit == 0)
                    {
                        return 0;
                    }
                }
            }
        }

        EventListener::EventListener()
        {
            ListenerState* state = ::new (static_cast<void*>(_storage)) ListenerState();
            state->epoll = -1;
            state->wake = -1;
        }

        EventListener::~EventListener()
        {
            close();
        }

        SocketCode EventListener::open(size_t completion_capacity)
        {
            ListenerState& state = stateOf(_storage);
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
            ListenerState& state = stateOf(_storage);
            while (state.ready_head != nullptr)
            {
                EpollEvent* event = state.ready_head;
                state.ready_head = event->next;
                event->next = nullptr;
                event->queued = false;
                event->disarm();
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

        AttachResult EventListener::attach(SocketHandle socket, int32_t slot)
        {
            ListenerState& state = stateOf(_storage);
            if (state.epoll < 0)
            {
                return error(SocketCode::CLOSED);
            }
            if (socket == INVALID_SOCKET_HANDLE)
            {
                return error(SocketCode::INVALID_ARGUMENT);
            }
            int copy = ::fcntl(static_cast<int>(socket), F_DUPFD_CLOEXEC, 0);
            if (copy < 0)
            {
                return error(lastSocketCode());
            }
            return AttachResult(AttachResult::Success(), Holder<IoEvent>::make<EpollEvent>(&state, copy, slot));
        }

        SocketCode EventListener::submit(Holder<IoEvent>& event, const IoAction& action, IoPayload payload)
        {
            ListenerState& state = stateOf(_storage);
            if (!event)
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            if (state.epoll < 0)
            {
                return SocketCode::CLOSED;
            }
            EpollEvent& epoll = static_cast<EpollEvent&>(*event);
            if (!epoll.belongsTo(state) || epoll.fd < 0)
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            if (epoll.pending())
            {
                return SocketCode::BUSY;
            }
            SocketCode checked = checkRequest(action, payload);
            if (checked != SocketCode::SUCCESS)
            {
                return checked;
            }
            epoll.arm(action, payload);
            SocketCode started = SocketCode::WOULD_BLOCK;
            if (action.kind == IoActionKind::WRITE)
            {
                started = sendRest(epoll);
            }
            if (started == SocketCode::WOULD_BLOCK)
            {
                started = watch(state, epoll);
                if (started == SocketCode::SUCCESS)
                {
                    return SocketCode::SUCCESS;
                }
            }
            queueReady(state, epoll, started);
            return SocketCode::SUCCESS;
        }

        void EventListener::detach(Holder<IoEvent>& event)
        {
            ListenerState& state = stateOf(_storage);
            if (!event)
            {
                return;
            }
            EpollEvent& epoll = static_cast<EpollEvent&>(*event);
            if (!epoll.belongsTo(state) || epoll.fd < 0)
            {
                return;
            }
            if (epoll.registered)
            {
                ::epoll_ctl(state.epoll, EPOLL_CTL_DEL, epoll.fd, nullptr);
                epoll.registered = false;
            }
            if (epoll.pending())
            {
                queueReady(state, epoll, SocketCode::CANCELLED);
            }
            ::close(epoll.fd);
            epoll.fd = -1;
        }

        Span<const IoCompletion> EventListener::wait(uint32_t timeout_ms)
        {
            ListenerState& state = stateOf(_storage);
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
            completions.length = poll(state, state.capacity, timeout_ms);
            return completions;
        }

        SocketCode EventListener::wake()
        {
            ListenerState& state = stateOf(_storage);
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
}
