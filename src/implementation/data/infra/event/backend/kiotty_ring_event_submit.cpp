#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_URING

#include "data/infra/event/backend/kiotty_ring_event_parts.h"

#include <sys/socket.h>
#include <sys/syscall.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

namespace kiotty::net::implementation
{
    namespace
    {
        static constexpr size_t LARGEST_LENGTH = 0xFFFFFFFFu;
        static constexpr int    FORGET_ROUNDS = 500;
        static constexpr long   FORGET_SLICE_NANOSECONDS = 10000000L;

        bool isSubmitFull(const RingState& ring);
        void track(EventListenerState& state, IoEvent& event, bool polling);
        bool ignoreCompletion(RingState& ring, uint64_t tag);
    }

    io_uring_sqe* backend::nextSqe(EventListenerState& state)
    {
        RingState& ring = state.ring;
        if (isSubmitFull(ring)) { static_cast<void>(enterRing(state, 0, nullptr)); }
        if (isSubmitFull(ring)) { return nullptr; }
        const uint32_t index = ring.local_tail & ring.sq_mask;
        io_uring_sqe* const sqe = static_cast<io_uring_sqe*>(ring.sqes) + index;
        *sqe = io_uring_sqe{};
        ring.sq_array[index] = index;
        ring.local_tail += 1;
        return sqe;
    }

    int backend::enterRing(EventListenerState& state, uint32_t min_complete,
        const __kernel_timespec* timeout)
    {
        RingState& ring = state.ring;
        __atomic_store_n(ring.sq_tail, ring.local_tail, __ATOMIC_RELEASE);
        const uint32_t waiting = ring.local_tail - __atomic_load_n(ring.sq_head, __ATOMIC_ACQUIRE);
        if (waiting == 0 && min_complete == 0) { return 0; }
        io_uring_getevents_arg argument{};
        argument.ts = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(timeout));
        const bool is_waiting = min_complete > 0;
        const unsigned flags = is_waiting ? IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG : 0u;
        const void* const extra = is_waiting ? &argument : nullptr;
        const size_t extra_bytes = is_waiting ? sizeof(argument) : 0;
        const long entered = ::syscall(__NR_io_uring_enter, ring.fd, waiting, min_complete,
            flags, extra, extra_bytes);
        if (entered < 0) { return errno; }
        return 0;
    }

    bool backend::startIo(EventListenerState& state, IoEvent& event)
    {
        io_uring_sqe* const sqe = nextSqe(state);
        if (sqe == nullptr) { return false; }
        const bool is_write = event.action.kind == IoActionKind::WRITE;
        sqe->fd = event.fd;
        sqe->user_data = tagOf(event);
        if (is_write)
        {
            const msghdr* const message = restMessage(event);
            sqe->opcode = static_cast<uint8_t>(IORING_OP_SENDMSG);
            sqe->addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(message));
            sqe->len = 1;
            sqe->msg_flags = static_cast<uint32_t>(MSG_NOSIGNAL);
            track(state, event, false);
            return true;
        }
        char* const bytes = static_cast<char*>(event.payload.first.buffer);
        const size_t rest = std::min(event.payload.first.length, LARGEST_LENGTH);
        sqe->opcode = static_cast<uint8_t>(IORING_OP_RECV);
        sqe->addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(bytes));
        sqe->len = static_cast<uint32_t>(rest);
        track(state, event, false);
        return true;
    }

    bool backend::startPoll(EventListenerState& state, IoEvent& event)
    {
        io_uring_sqe* const sqe = nextSqe(state);
        if (sqe == nullptr) { return false; }
        const bool is_write = event.action.kind == IoActionKind::WRITE;
        sqe->opcode = static_cast<uint8_t>(IORING_OP_POLL_ADD);
        sqe->fd = event.fd;
        sqe->poll32_events = static_cast<uint32_t>(is_write ? POLLOUT : POLLIN);
        sqe->user_data = tagOf(event);
        track(state, event, true);
        return true;
    }

    void backend::armWake(EventListenerState& state)
    {
        io_uring_sqe* const sqe = nextSqe(state);
        if (sqe == nullptr) { return; }
        sqe->opcode = static_cast<uint8_t>(IORING_OP_READ);
        sqe->fd = state.wake;
        sqe->addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&state.ring.wake_value));
        sqe->len = static_cast<uint32_t>(sizeof(state.ring.wake_value));
        sqe->user_data = WAKE_TAG;
        state.ring.wake_armed = true;
    }

    SocketCode backend::ringWatch(EventListenerState& state, IoEvent& event)
    {
        const bool is_accept = event.action.kind == IoActionKind::ACCEPT;
        const bool started = is_accept ? startPoll(state, event) : startIo(state, event);
        return started ? SocketCode::SUCCESS : SocketCode::OUT_OF_RESOURCE;
    }

    void backend::ringCancel(EventListenerState& state, IoEvent& event)
    {
        if (!event.registered) { return; }
        io_uring_sqe* const sqe = nextSqe(state);
        if (sqe == nullptr) { return; }
        sqe->opcode = static_cast<uint8_t>(IORING_OP_ASYNC_CANCEL);
        sqe->fd = -1;
        sqe->addr = tagOf(event);
        sqe->user_data = CANCEL_TAG;
    }

    void backend::ringForget(EventListenerState& state, IoEvent& event)
    {
        ringCancel(state, event);
        const __kernel_timespec slice = { 0, FORGET_SLICE_NANOSECONDS };
        const uint64_t tag = tagOf(event);
        for (int round = 0; round < FORGET_ROUNDS; ++round)
        {
            if (ignoreCompletion(state.ring, tag))
            {
                state.ring.inflight -= 1;
                event.registered = false;
                return;
            }
            static_cast<void>(enterRing(state, readyCount(state.ring) + 1, &slice));
        }
    }

    namespace
    {
        bool isSubmitFull(const RingState& ring)
        {
            const uint32_t head = __atomic_load_n(ring.sq_head, __ATOMIC_ACQUIRE);
            return ring.local_tail - head >= ring.sq_entries;
        }

        void track(EventListenerState& state, IoEvent& event, bool polling)
        {
            event.registered = true;
            event.polling = polling;
            state.ring.inflight += 1;
        }

        bool ignoreCompletion(RingState& ring, uint64_t tag)
        {
            const uint32_t tail = __atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE);
            for (uint32_t position = *ring.cq_head; position != tail; ++position)
            {
                io_uring_cqe* const cqe = backend::cqeAt(ring, position);
                if (cqe->user_data != tag) { continue; }
                cqe->user_data = backend::IGNORE_TAG;
                return true;
            }
            return false;
        }
    }
}

#endif
