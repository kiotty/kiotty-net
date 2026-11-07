#if defined(__linux__)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_epoll_event_state.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>

namespace kiotty::net::implementation
{
    using backend::disarm;
    using backend::sendRest;
    using backend::stateOf;
    using backend::watch;

    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Milliseconds = std::chrono::milliseconds;

        size_t       drainReady(EventListenerState& state);
        size_t       poll(EventListenerState& state, size_t room, uint32_t timeout_milliseconds);
        size_t       collect(EventListenerState& state, int ready);
        bool         isWaiting(const IoEvent& event);
        SocketCode   advance(EventListenerState& state, IoEvent& event);
        SocketCode   progress(IoEvent& event);
        SocketCode   receiveOnce(IoEvent& event);
        IoKind       kindOf(const IoAction& action);
        IoCompletion completionOf(IoEvent& event, SocketCode code);
        int          remainingMilliseconds(Clock::time_point deadline);
        void         clearWake(EventListenerState& state);
    }

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_milliseconds)
    {
        EventListenerState& state = stateOf(_storage);
        Span<const IoCompletion> completions = { state.completions, 0 };
        if (state.epoll < 0)
        {
            return completions;
        }
        const size_t ready = drainReady(state);
        if (ready > 0)
        {
            completions.length = ready;
            return completions;
        }
        completions.length = poll(state, state.capacity, timeout_milliseconds);
        return completions;
    }

    namespace
    {
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

        size_t poll(EventListenerState& state, size_t room, uint32_t timeout_milliseconds)
        {
            const bool forever = timeout_milliseconds == EventListener::WAIT_FOREVER;
            const Milliseconds timeout(timeout_milliseconds);
            const Clock::time_point deadline = Clock::now() + timeout;
            const int capacity = static_cast<int>(room);
            int limit = forever ? -1 : remainingMilliseconds(deadline);
            for (;;)
            {
                const int ready = ::epoll_wait(state.epoll, state.entries, capacity, limit);
                if (ready < 0 && errno != EINTR) { return 0; }
                const size_t count = collect(state, ready);
                if (count > 0) { return count; }
                if (forever) { continue; }
                limit = remainingMilliseconds(deadline);
                if (limit == 0) { return 0; }
            }
        }

        size_t collect(EventListenerState& state, int ready)
        {
            size_t count = 0;
            for (int i = 0; i < ready; ++i)
            {
                void* const tag = state.entries[i].data.ptr;
                if (tag == nullptr)
                {
                    clearWake(state);
                    state.completions[count] = IoCompletion::wake();
                    count += 1;
                    continue;
                }
                IoEvent& event = *static_cast<IoEvent*>(tag);
                if (!isWaiting(event)) { continue; }
                const SocketCode code = advance(state, event);
                if (code == SocketCode::WOULD_BLOCK) { continue; }
                state.completions[count] = completionOf(event, code);
                count += 1;
            }
            return count;
        }

        bool isWaiting(const IoEvent& event)
        {
            return event.pending && !event.queued;
        }

        SocketCode advance(EventListenerState& state, IoEvent& event)
        {
            const SocketCode advanced = progress(event);
            if (advanced != SocketCode::WOULD_BLOCK)
            {
                return advanced;
            }
            const SocketCode watched = watch(state, event);
            if (watched == SocketCode::SUCCESS)
            {
                return SocketCode::WOULD_BLOCK;
            }
            return watched;
        }

        SocketCode progress(IoEvent& event)
        {
            switch (event.action.kind)
            {
            case IoActionKind::WRITE:
                return sendRest(event);
            case IoActionKind::ACCEPT:
                return SocketCode::SUCCESS;
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::NONE:
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return receiveOnce(event);
            }
            return receiveOnce(event);
        }

        SocketCode receiveOnce(IoEvent& event)
        {
            for (;;)
            {
                const ssize_t received =
                    ::recv(event.fd, event.payload.buffer, event.payload.length, 0);
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

        IoKind kindOf(const IoAction& action)
        {
            switch (action.kind)
            {
            case IoActionKind::ACCEPT:
                return IoKind::ACCEPT;
            case IoActionKind::WRITE:
                return IoKind::SEND;
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::NONE:
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return IoKind::RECEIVE;
            }
            return IoKind::RECEIVE;
        }

        IoCompletion completionOf(IoEvent& event, SocketCode code)
        {
            const IoKind kind = kindOf(event.action);
            IoCompletion completion = IoCompletion::of(kind, event.slot, event.action);
            completion.code = code;
            completion.length = event.done;
            disarm(event);
            return completion;
        }

        int remainingMilliseconds(Clock::time_point deadline)
        {
            const Clock::duration left = deadline - Clock::now();
            if (left <= Clock::duration::zero())
            {
                return 0;
            }
            Milliseconds whole = std::chrono::duration_cast<Milliseconds>(left);
            if (whole < left)
            {
                whole += Milliseconds(1);
            }
            if (whole.count() > INT_MAX)
            {
                return INT_MAX;
            }
            return static_cast<int>(whole.count());
        }

        void clearWake(EventListenerState& state)
        {
            uint64_t counter = 0;
            const ssize_t consumed = ::read(state.wake, &counter, sizeof(counter));
            static_cast<void>(consumed);
        }
    }
}

#endif
