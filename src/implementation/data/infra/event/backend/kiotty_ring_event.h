#pragma once

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_EPOLL

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct RingState
    {
        RingState();

        int       fd;
        void*     rings;
        size_t    ring_bytes;
        void*     sqes;
        size_t    sqe_bytes;
        void*     cqes;
        uint32_t* sq_head;
        uint32_t* sq_tail;
        uint32_t* sq_array;
        uint32_t* cq_head;
        uint32_t* cq_tail;
        uint32_t  sq_mask;
        uint32_t  sq_entries;
        uint32_t  cq_mask;
        uint32_t  local_tail;
        size_t    inflight;
        uint64_t  wake_value;
        bool      wake_armed;
    };

    inline RingState::RingState()
        : fd(-1)
        , rings(nullptr)
        , ring_bytes(0)
        , sqes(nullptr)
        , sqe_bytes(0)
        , cqes(nullptr)
        , sq_head(nullptr)
        , sq_tail(nullptr)
        , sq_array(nullptr)
        , cq_head(nullptr)
        , cq_tail(nullptr)
        , sq_mask(0)
        , sq_entries(0)
        , cq_mask(0)
        , local_tail(0)
        , inflight(0)
        , wake_value(0)
        , wake_armed(false)
    {
    }
}

#endif
