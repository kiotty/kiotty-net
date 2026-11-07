#pragma once

#if !defined(_WIN32) && !defined(__linux__)

#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_io_payload.h"

namespace kiotty::net::implementation
{
    struct IoEvent
    {
        IoEvent();
        IoEvent(const IoEvent&) = delete;
        IoEvent& operator=(const IoEvent&) = delete;

        bool     pending;
        IoAction action;
    };

    inline IoEvent::IoEvent()
        : pending(false)
        , action(IoAction::none())
    {
    }
}

#endif
