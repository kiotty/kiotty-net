#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_EPOLL

#include "data/infra/event/backend/kiotty_ring_event_state.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <cerrno>

namespace kiotty::net::implementation
{
    namespace
    {
        size_t collect(EventListenerState& state, int ready);
        void   clearWake(EventListenerState& state);
    }

    size_t backend::poll(EventListenerState& state, uint32_t timeout_milliseconds)
    {
        if constexpr (RING_BACKEND)
        {
            if (isRing(state)) { return ringPoll(state, timeout_milliseconds); }
        }
        const bool forever = isForever(timeout_milliseconds);
        const Clock::time_point deadline =
            Clock::now() + std::chrono::milliseconds(timeout_milliseconds);
        const int capacity = static_cast<int>(state.capacity);
        int limit = forever ? -1 : remainingMilliseconds(deadline);
        for (;;)
        {
            const int ready = ::epoll_wait(state.epoll, state.entries.data(), capacity, limit);
            if (ready < 0 && errno != EINTR) { return 0; }
            const size_t count = collect(state, ready);
            if (count > 0) { return count; }
            if (forever) { continue; }
            limit = remainingMilliseconds(deadline);
            if (limit == 0) { return 0; }
        }
    }

    namespace
    {
        size_t collect(EventListenerState& state, int ready)
        {
            size_t count = 0;
            for (int i = 0; i < ready; ++i)
            {
                void* const tag = state.entries[static_cast<size_t>(i)].data.ptr;
                if (tag == nullptr)
                {
                    clearWake(state);
                    count = backend::noteWake(state, count);
                    continue;
                }
                IoEvent& event = *static_cast<IoEvent*>(tag);
                count = backend::settle(state, event, count, SocketCode::SUCCESS);
            }
            return count;
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
