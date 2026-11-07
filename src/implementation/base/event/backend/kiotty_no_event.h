#if !defined(KIOTTY_NO_EVENT_H)
#define KIOTTY_NO_EVENT_H

#if !defined(_WIN32) && !defined(__linux__)

#include "base/event/kiotty_event.h"

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

#endif
