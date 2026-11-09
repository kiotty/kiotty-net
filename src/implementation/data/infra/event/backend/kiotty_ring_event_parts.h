#pragma once

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_URING

#include "data/infra/event/backend/kiotty_ring_event_state.h"

#include <linux/io_uring.h>

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    namespace backend
    {
        static constexpr uint64_t WAKE_TAG = 1;
        static constexpr uint64_t CANCEL_TAG = 2;
        static constexpr uint64_t IGNORE_TAG = 3;

        io_uring_sqe* nextSqe(EventListenerState& state);
        int           enterRing(EventListenerState& state, uint32_t min_complete,
                                const __kernel_timespec* timeout);
        bool          startIo(EventListenerState& state, IoEvent& event);
        bool          startPoll(EventListenerState& state, IoEvent& event);
        void          armWake(EventListenerState& state);
        void          drainForClose(EventListenerState& state);
        uint64_t      tagOf(const IoEvent& event);
        io_uring_cqe* cqeAt(const RingState& ring, uint32_t position);
        uint32_t      readyCount(const RingState& ring);
    }

    inline uint64_t backend::tagOf(const IoEvent& event)
    {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&event));
    }

    inline io_uring_cqe* backend::cqeAt(const RingState& ring, uint32_t position)
    {
        io_uring_cqe* const first = static_cast<io_uring_cqe*>(ring.cqes);
        return first + (position & ring.cq_mask);
    }

    inline uint32_t backend::readyCount(const RingState& ring)
    {
        return __atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE) - *ring.cq_head;
    }
}

#endif
