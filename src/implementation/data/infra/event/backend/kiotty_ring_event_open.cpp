#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_URING

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_ring_event_parts.h"

#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>

namespace kiotty::net::implementation
{
    namespace
    {
        static constexpr uint32_t MIN_ENTRIES = 8;
        static constexpr uint32_t MAX_ENTRIES = 4096;
        static constexpr uint32_t PROBE_OPS = 256;
        static constexpr uint32_t REQUIRED_FEATURES =
            IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP | IORING_FEAT_EXT_ARG;
        static constexpr uint8_t REQUIRED_OPS[] = {
            IORING_OP_RECV, IORING_OP_SEND, IORING_OP_POLL_ADD, IORING_OP_ASYNC_CANCEL,
            IORING_OP_READ
        };

        uint32_t   entriesFor(size_t completion_capacity);
        SocketCode setup(RingState& ring, uint32_t entries);
        SocketCode mapRings(RingState& ring, const io_uring_params& params);
        void       bindRings(RingState& ring, const io_uring_params& params);
        bool       hasOperations(int fd);
        void       cancelAll(EventListenerState& state);
    }

    SocketCode backend::ringOpen(EventListenerState& state, size_t completion_capacity)
    {
        SocketCode code = setup(state.ring, entriesFor(completion_capacity));
        if (code == SocketCode::SUCCESS)
        {
            state.wake = ::eventfd(0, EFD_CLOEXEC);
            if (state.wake >= 0) { return SocketCode::SUCCESS; }
            code = lastSocketCode();
        }
        ringClose(state);
        return code;
    }

    void backend::ringClose(EventListenerState& state)
    {
        RingState& ring = state.ring;
        if (ring.fd < 0) { return; }
        if (ring.sqes != nullptr) { cancelAll(state); }
        if (ring.sqes != nullptr) { ::munmap(ring.sqes, ring.sqe_bytes); }
        if (ring.rings != nullptr) { ::munmap(ring.rings, ring.ring_bytes); }
        ::close(ring.fd);
        ring = RingState();
    }

    namespace
    {
        uint32_t entriesFor(size_t completion_capacity)
        {
            uint32_t entries = MIN_ENTRIES;
            while (entries < completion_capacity && entries < MAX_ENTRIES)
            {
                entries *= 2;
            }
            return entries;
        }

        SocketCode setup(RingState& ring, uint32_t entries)
        {
            io_uring_params params{};
            const long created = ::syscall(__NR_io_uring_setup, entries, &params);
            if (created < 0) { return lastSocketCode(); }
            ring.fd = static_cast<int>(created);
            const bool has_features = (params.features & REQUIRED_FEATURES) == REQUIRED_FEATURES;
            if (!has_features || !hasOperations(ring.fd)) { return SocketCode::NOT_SUPPORTED; }
            return mapRings(ring, params);
        }

        SocketCode mapRings(RingState& ring, const io_uring_params& params)
        {
            const size_t submissions = params.sq_off.array + params.sq_entries * sizeof(uint32_t);
            const size_t completions =
                params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);
            const int protection = PROT_READ | PROT_WRITE;
            const int sharing = MAP_SHARED | MAP_POPULATE;
            const off_t rings_offset = static_cast<off_t>(IORING_OFF_SQ_RING);
            const off_t sqes_offset = static_cast<off_t>(IORING_OFF_SQES);
            ring.ring_bytes = std::max(submissions, completions);
            void* const rings =
                ::mmap(nullptr, ring.ring_bytes, protection, sharing, ring.fd, rings_offset);
            if (rings == MAP_FAILED) { return lastSocketCode(); }
            ring.rings = rings;
            ring.sqe_bytes = params.sq_entries * sizeof(io_uring_sqe);
            void* const sqes =
                ::mmap(nullptr, ring.sqe_bytes, protection, sharing, ring.fd, sqes_offset);
            if (sqes == MAP_FAILED) { return lastSocketCode(); }
            ring.sqes = sqes;
            bindRings(ring, params);
            return SocketCode::SUCCESS;
        }

        void bindRings(RingState& ring, const io_uring_params& params)
        {
            char* const base = static_cast<char*>(ring.rings);
            ring.sq_head = reinterpret_cast<uint32_t*>(base + params.sq_off.head);
            ring.sq_tail = reinterpret_cast<uint32_t*>(base + params.sq_off.tail);
            ring.sq_array = reinterpret_cast<uint32_t*>(base + params.sq_off.array);
            ring.sq_mask = *reinterpret_cast<const uint32_t*>(base + params.sq_off.ring_mask);
            ring.cq_head = reinterpret_cast<uint32_t*>(base + params.cq_off.head);
            ring.cq_tail = reinterpret_cast<uint32_t*>(base + params.cq_off.tail);
            ring.cq_mask = *reinterpret_cast<const uint32_t*>(base + params.cq_off.ring_mask);
            ring.cqes = base + params.cq_off.cqes;
            ring.sq_entries = params.sq_entries;
            ring.local_tail = *ring.sq_tail;
        }

        bool hasOperations(int fd)
        {
            alignas(io_uring_probe) unsigned char
                bytes[sizeof(io_uring_probe) + PROBE_OPS * sizeof(io_uring_probe_op)] = {};
            io_uring_probe* const probe = reinterpret_cast<io_uring_probe*>(bytes);
            const long probed =
                ::syscall(__NR_io_uring_register, fd, IORING_REGISTER_PROBE, probe, PROBE_OPS);
            if (probed < 0) { return false; }
            const io_uring_probe_op* const ops =
                reinterpret_cast<const io_uring_probe_op*>(bytes + sizeof(io_uring_probe));
            for (const uint8_t op : REQUIRED_OPS)
            {
                const bool is_known = op <= probe->last_op;
                if (!is_known || (ops[op].flags & IO_URING_OP_SUPPORTED) == 0) { return false; }
            }
            return true;
        }

#if defined(IORING_ASYNC_CANCEL_ANY)
        void cancelAll(EventListenerState& state)
        {
            if (state.ring.inflight == 0) { return; }
            io_uring_sqe* const sqe = backend::nextSqe(state);
            if (sqe != nullptr)
            {
                sqe->opcode = static_cast<uint8_t>(IORING_OP_ASYNC_CANCEL);
                sqe->fd = -1;
                sqe->cancel_flags = IORING_ASYNC_CANCEL_ANY;
                sqe->user_data = backend::CANCEL_TAG;
            }
            backend::drainForClose(state);
        }
#else
        void cancelAll(EventListenerState& state)
        {
            static_cast<void>(state);
        }
#endif
    }
}

#endif
