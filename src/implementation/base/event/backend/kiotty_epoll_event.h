#if !defined(KIOTTY_EPOLL_EVENT_H)
#define KIOTTY_EPOLL_EVENT_H

#if defined(__linux__)

#include "base/event/kiotty_event.h"

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        struct EventListenerState;

        struct IoEvent
        {
            IoEvent();
            ~IoEvent();
            IoEvent(const IoEvent&) = delete;
            IoEvent& operator=(const IoEvent&) = delete;

            bool                pending;
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
    }
}

#endif

#endif
