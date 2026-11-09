#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_READINESS

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <climits>

namespace kiotty::net::implementation
{
    using backend::sendRest;
    using backend::watch;

    namespace
    {
        using Milliseconds = std::chrono::milliseconds;

        bool       isWaiting(const IoEvent& event);
        SocketCode advance(EventListenerState& state, IoEvent& event);
        SocketCode progress(IoEvent& event);
        SocketCode receiveOnce(IoEvent& event);
    }

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_milliseconds)
    {
        EventListenerState& state = _state;
        Span<const IoCompletion> completions = { state.completions.data(), 0 };
        if (!backend::isOpen(state))
        {
            return completions;
        }
        const size_t ready = backend::drainReady(state);
        if (ready > 0)
        {
            completions.length = ready;
            return completions;
        }
        completions.length = backend::poll(state, timeout_milliseconds);
        return completions;
    }

    size_t backend::settle(EventListenerState& state, IoEvent& event, size_t count,
        SocketCode eof_error)
    {
        if (!isWaiting(event)) { return count; }
        SocketCode code = advance(state, event);
        if (code == SocketCode::WOULD_BLOCK) { return count; }
        if (code == SocketCode::CLOSED && eof_error != SocketCode::SUCCESS) { code = eof_error; }
        state.completions[count] = completionOf(event, code);
        return count + 1;
    }

    size_t backend::noteWake(EventListenerState& state, size_t count)
    {
        state.completions[count] = IoCompletion::wake();
        return count + 1;
    }

    int backend::remainingMilliseconds(Clock::time_point deadline)
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

    namespace
    {
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
    }
}

#endif
