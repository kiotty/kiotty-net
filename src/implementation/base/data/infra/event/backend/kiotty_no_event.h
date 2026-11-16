#pragma once

#include "base/data/infra/event/backend/kiotty_event_backend.h"

#if !KIOTTY_NET_HAS_EVENT_BACKEND

#include "base/domain/entity/kiotty_io_completion.h"
#include "base/domain/entity/kiotty_io_payload.h"

#include <atomic>

namespace kiotty::net::implementation
{
    struct IoEvent
    {
        IoEvent();
        IoEvent(const IoEvent&) = delete;
        IoEvent& operator=(const IoEvent&) = delete;

        std::atomic<bool> pending;
        IoAction action;
    };

    struct EventListenerState
    {
    };

    inline IoEvent::IoEvent()
        : pending(false)
        , action(IoAction::none())
    {
    }
}

#endif
