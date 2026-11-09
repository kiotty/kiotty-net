#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_URING

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_ring_event_parts.h"

#include <cerrno>
#include <chrono>

namespace kiotty::net::implementation
{
    namespace
    {
        static constexpr int  CLOSE_ROUNDS = 50;
        static constexpr long CLOSE_SLICE_NANOSECONDS = 20000000L;
        static constexpr int  MILLISECONDS_PER_SECOND = 1000;
        static constexpr long NANOSECONDS_PER_MILLISECOND = 1000000L;

        size_t     collect(EventListenerState& state);
        size_t     settleRing(EventListenerState& state, uint64_t tag, int32_t result,
                              size_t count);
        size_t     settleWake(EventListenerState& state, int32_t result, size_t count);
        SocketCode finish(EventListenerState& state, IoEvent& event, int32_t result);
        SocketCode progressed(EventListenerState& state, IoEvent& event, size_t moved);
        SocketCode restart(EventListenerState& state, IoEvent& event, bool polling);
        bool       waitRing(EventListenerState& state, backend::Clock::time_point deadline,
                            bool forever);
        void       settleForClose(EventListenerState& state);
        IoEvent&   eventOf(uint64_t tag);
    }

    size_t backend::ringPoll(EventListenerState& state, uint32_t timeout_milliseconds)
    {
        const bool forever = isForever(timeout_milliseconds);
        const Clock::time_point deadline =
            Clock::now() + std::chrono::milliseconds(timeout_milliseconds);
        size_t count = collect(state);
        while (count == 0 && waitRing(state, deadline, forever))
        {
            count = collect(state);
        }
        if (count == 0) { count = collect(state); }
        static_cast<void>(enterRing(state, 0, nullptr));
        return count;
    }

    void backend::drainForClose(EventListenerState& state)
    {
        const __kernel_timespec slice = { 0, CLOSE_SLICE_NANOSECONDS };
        for (int round = 0; round < CLOSE_ROUNDS && state.ring.inflight > 0; ++round)
        {
            static_cast<void>(enterRing(state, 1, &slice));
            settleForClose(state);
        }
    }

    namespace
    {
        size_t collect(EventListenerState& state)
        {
            RingState& ring = state.ring;
            uint32_t head = *ring.cq_head;
            const uint32_t tail = __atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE);
            size_t count = 0;
            while (head != tail && count < state.capacity)
            {
                const io_uring_cqe* const cqe = backend::cqeAt(ring, head);
                const uint64_t tag = cqe->user_data;
                const int32_t result = cqe->res;
                head += 1;
                __atomic_store_n(ring.cq_head, head, __ATOMIC_RELEASE);
                count = settleRing(state, tag, result, count);
            }
            return count;
        }

        size_t settleRing(EventListenerState& state, uint64_t tag, int32_t result, size_t count)
        {
            if (tag == backend::WAKE_TAG) { return settleWake(state, result, count); }
            if (tag == backend::CANCEL_TAG || tag == backend::IGNORE_TAG) { return count; }
            IoEvent& event = eventOf(tag);
            state.ring.inflight -= 1;
            event.registered = false;
            const SocketCode code = finish(state, event, result);
            if (code == SocketCode::WOULD_BLOCK) { return count; }
            state.completions[count] = backend::completionOf(event, code);
            return count + 1;
        }

        size_t settleWake(EventListenerState& state, int32_t result, size_t count)
        {
            state.ring.wake_armed = false;
            if (result < 0) { return count; }
            backend::armWake(state);
            return backend::noteWake(state, count);
        }

        SocketCode finish(EventListenerState& state, IoEvent& event, int32_t result)
        {
            if (!backend::attached(event) || result == -ECANCELED) { return SocketCode::CANCELLED; }
            if (result == -EAGAIN) { return restart(state, event, true); }
            if (result == -EINTR) { return restart(state, event, event.polling); }
            if (result < 0) { return toSocketCode(-result); }
            if (!event.polling) { return progressed(state, event, static_cast<size_t>(result)); }
            if (event.action.kind == IoActionKind::ACCEPT) { return SocketCode::SUCCESS; }
            return restart(state, event, false);
        }

        SocketCode progressed(EventListenerState& state, IoEvent& event, size_t moved)
        {
            if (event.action.kind != IoActionKind::WRITE)
            {
                event.done = moved;
                return moved == 0 ? SocketCode::CLOSED : SocketCode::SUCCESS;
            }
            event.done += moved;
            if (event.done < event.payload.length) { return restart(state, event, false); }
            return SocketCode::SUCCESS;
        }

        SocketCode restart(EventListenerState& state, IoEvent& event, bool polling)
        {
            const bool started =
                polling ? backend::startPoll(state, event) : backend::startIo(state, event);
            return started ? SocketCode::WOULD_BLOCK : SocketCode::OUT_OF_RESOURCE;
        }

        bool waitRing(EventListenerState& state, backend::Clock::time_point deadline,
            bool forever)
        {
            if (!state.ring.wake_armed) { backend::armWake(state); }
            const int left = forever ? -1 : backend::remainingMilliseconds(deadline);
            if (left == 0)
            {
                static_cast<void>(backend::enterRing(state, 0, nullptr));
                return false;
            }
            const __kernel_timespec limit = { left / MILLISECONDS_PER_SECOND,
                (left % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND };
            const int failure = backend::enterRing(state, 1, forever ? nullptr : &limit);
            const bool is_soft = failure == ETIME || failure == EINTR || failure == EBUSY;
            return failure == 0 || is_soft || failure == EAGAIN;
        }

        void settleForClose(EventListenerState& state)
        {
            RingState& ring = state.ring;
            const uint32_t tail = __atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE);
            for (uint32_t head = *ring.cq_head; head != tail; ++head)
            {
                const uint64_t tag = backend::cqeAt(ring, head)->user_data;
                if (tag <= backend::IGNORE_TAG) { continue; }
                IoEvent& event = eventOf(tag);
                event.registered = false;
                backend::disarm(event);
                ring.inflight -= 1;
            }
            __atomic_store_n(ring.cq_head, tail, __ATOMIC_RELEASE);
        }

        IoEvent& eventOf(uint64_t tag)
        {
            return *reinterpret_cast<IoEvent*>(static_cast<uintptr_t>(tag));
        }
    }
}

#endif
