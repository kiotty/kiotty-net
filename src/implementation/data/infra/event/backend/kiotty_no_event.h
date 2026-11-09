#pragma once

#if !defined(_WIN32) && !defined(__linux__)

#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_io_payload.h"

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
