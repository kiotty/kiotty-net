#pragma once

#if defined(__linux__)

#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_io_payload.h"

#include <sys/epoll.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    struct EventListenerState;

    struct IoEvent
    {
        IoEvent();
        ~IoEvent();
        IoEvent(const IoEvent&) = delete;
        IoEvent& operator=(const IoEvent&) = delete;

        std::atomic<bool>   pending;
        IoAction            action;
        EventListenerState* listener;
        uint32_t            generation;
        int                 fd;
        int32_t             slot;
        bool                registered;
        bool                queued;
        SocketCode          result;
        IoPayload           payload;
        size_t              done;
        IoEvent*            next;
    };

    inline IoEvent::IoEvent()
        : pending(false)
        , action(IoAction::none())
        , listener(nullptr)
        , generation(0)
        , fd(-1)
        , slot(-1)
        , registered(false)
        , queued(false)
        , result(SocketCode::SUCCESS)
        , payload()
        , done(0)
        , next(nullptr)
    {
    }

    struct EventListenerState
    {
        EventListenerState();

        int                       epoll;
        int                       wake;
        std::vector<epoll_event>  entries;
        std::vector<IoCompletion> completions;
        size_t                    capacity;
        uint32_t                  generation;
        IoEvent*                  ready_head;
        IoEvent*                  ready_tail;
    };

    inline EventListenerState::EventListenerState()
        : epoll(-1)
        , wake(-1)
        , entries()
        , completions()
        , capacity(0)
        , generation(0)
        , ready_head(nullptr)
        , ready_tail(nullptr)
    {
    }
}

#endif
