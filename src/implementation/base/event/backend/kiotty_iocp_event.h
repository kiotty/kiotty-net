#if !defined(KIOTTY_IOCP_EVENT_H)
#define KIOTTY_IOCP_EVENT_H

#if defined(_WIN32)

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "base/event/kiotty_event.h"

#include <winsock2.h>
#include <mswsock.h>

#include <cstdint>

namespace kiotty
{
    namespace net
    {
        struct EventListenerState;
        struct IoEvent;

        struct IocpRequest
        {
            OVERLAPPED overlapped;
            IoEvent*   owner;
        };

        struct IoEvent
        {
            IoEvent();
            IoEvent(const IoEvent&) = delete;
            IoEvent& operator=(const IoEvent&) = delete;

            bool                      pending;
            IoAction                  action;
            const EventListenerState* listener;
            uint32_t                  generation;
            SOCKET                    socket;
            int32_t                   slot;
            SocketCode                failure;
            IocpRequest               request;
            LPFN_ACCEPTEX             accept_ex;
        };

        inline IoEvent::IoEvent()
            : pending(false)
            , action(IoAction::none())
            , listener(nullptr)
            , generation(0)
            , socket(INVALID_SOCKET)
            , slot(-1)
            , failure(SocketCode::SUCCESS)
            , request()
            , accept_ex(nullptr)
        {
        }
    }
}

#endif

#endif
