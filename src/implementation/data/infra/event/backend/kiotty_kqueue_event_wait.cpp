#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_KQUEUE

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <cerrno>

namespace kiotty::net::implementation
{
    namespace
    {
        size_t     collect(EventListenerState& state, int ready);
        SocketCode eofErrorOf(const struct kevent& entry);
        timespec   timeoutOf(int milliseconds);
    }

    size_t backend::poll(EventListenerState& state, uint32_t timeout_milliseconds)
    {
        const bool forever = isForever(timeout_milliseconds);
        const Clock::time_point deadline =
            Clock::now() + std::chrono::milliseconds(timeout_milliseconds);
        const int capacity = static_cast<int>(state.capacity);
        for (;;)
        {
            timespec limit = timeoutOf(forever ? 0 : remainingMilliseconds(deadline));
            const timespec* timeout = forever ? nullptr : &limit;
            const int ready =
                ::kevent(state.queue, nullptr, 0, state.entries.data(), capacity, timeout);
            if (ready < 0 && errno != EINTR) { return 0; }
            const size_t count = collect(state, ready);
            if (count > 0) { return count; }
            if (forever) { continue; }
            if (remainingMilliseconds(deadline) == 0) { return 0; }
        }
    }

    namespace
    {
        size_t collect(EventListenerState& state, int ready)
        {
            size_t count = 0;
            for (int i = 0; i < ready; ++i)
            {
                const struct kevent& entry = state.entries[static_cast<size_t>(i)];
                if (entry.filter == EVFILT_USER)
                {
                    count = backend::noteWake(state, count);
                    continue;
                }
                IoEvent& event = *static_cast<IoEvent*>(entry.udata);
                event.registered = false;
                count = backend::settle(state, event, count, eofErrorOf(entry));
            }
            return count;
        }

        SocketCode eofErrorOf(const struct kevent& entry)
        {
            const bool is_eof = (entry.flags & EV_EOF) != 0;
            if (!is_eof || entry.fflags == 0)
            {
                return SocketCode::SUCCESS;
            }
            return toSocketCode(static_cast<int>(entry.fflags));
        }

        timespec timeoutOf(int milliseconds)
        {
            timespec limit;
            limit.tv_sec = milliseconds / 1000;
            limit.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
            return limit;
        }
    }
}

#endif
