#pragma once

#if defined(_WIN32)

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_io_payload.h"

#include <winsock2.h>
#include <mswsock.h>

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
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

        std::atomic<bool>         pending;
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

    struct EventListenerState
    {
        EventListenerState();

        HANDLE                        port;
        std::vector<OVERLAPPED_ENTRY> entries;
        std::vector<IoCompletion>     completions;
        size_t                        capacity;
        uint32_t                      generation;
    };

    inline EventListenerState::EventListenerState()
        : port(nullptr)
        , entries()
        , completions()
        , capacity(0)
        , generation(0)
    {
    }
}

#endif
