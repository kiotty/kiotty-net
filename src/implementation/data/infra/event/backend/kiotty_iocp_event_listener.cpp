#if defined(_WIN32)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_iocp_event_state.h"

#include <winsock2.h>
#include <windows.h>

#include <climits>
#include <new>

namespace kiotty::net::implementation
{
    using backend::attached;
    using backend::belongsTo;
    using backend::stateOf;

    namespace
    {
        bool       isUsableCapacity(size_t completion_capacity);
        SocketCode allocate(EventListenerState& state, size_t completion_capacity);
        SocketCode checkAttach(const EventListenerState& state, const IoEvent& event,
            SocketHandle socket);
        SocketCode associate(const EventListenerState& state, SOCKET native);
        SocketCode lastErrorCode();
    }

    EventListener::EventListener()
    {
        ::new (static_cast<void*>(_storage)) EventListenerState();
    }

    EventListener::~EventListener()
    {
        close();
    }

    SocketCode EventListener::open(size_t completion_capacity)
    {
        EventListenerState& state = stateOf(_storage);
        if (state.port != nullptr) { return SocketCode::INVALID_ARGUMENT; }
        if (!isUsableCapacity(completion_capacity)) { return SocketCode::INVALID_ARGUMENT; }
        const SocketCode allocated = allocate(state, completion_capacity);
        if (allocated != SocketCode::SUCCESS)
        {
            close();
            return allocated;
        }
        state.port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (state.port == nullptr)
        {
            const SocketCode failed = lastErrorCode();
            close();
            return failed;
        }
        state.capacity = completion_capacity;
        state.generation += 1;
        return SocketCode::SUCCESS;
    }

    void EventListener::close()
    {
        EventListenerState& state = stateOf(_storage);
        if (state.port != nullptr)
        {
            ::CloseHandle(state.port);
        }
        delete[] state.entries;
        delete[] state.completions;
        state.port = nullptr;
        state.entries = nullptr;
        state.completions = nullptr;
        state.capacity = 0;
    }

    SocketCode EventListener::attach(IoEvent& event, SocketHandle socket, int32_t slot)
    {
        EventListenerState& state = stateOf(_storage);
        const SocketCode checked = checkAttach(state, event, socket);
        if (checked != SocketCode::SUCCESS) { return checked; }
        const SOCKET native = static_cast<SOCKET>(socket);
        const SocketCode associated = associate(state, native);
        if (associated != SocketCode::SUCCESS) { return associated; }
        event.listener = &state;
        event.generation = state.generation;
        event.socket = native;
        event.slot = slot;
        event.failure = SocketCode::SUCCESS;
        event.accept_ex = nullptr;
        return SocketCode::SUCCESS;
    }

    void EventListener::detach(IoEvent& event)
    {
        EventListenerState& state = stateOf(_storage);
        if (!attached(event) || event.listener != &state)
        {
            return;
        }
        if (event.pending && belongsTo(event, state))
        {
            ::CancelIoEx(reinterpret_cast<HANDLE>(event.socket), &event.request.overlapped);
        }
        event.socket = INVALID_SOCKET;
    }

    SocketCode EventListener::wake()
    {
        EventListenerState& state = stateOf(_storage);
        if (state.port == nullptr)
        {
            return SocketCode::CLOSED;
        }
        const BOOL posted = ::PostQueuedCompletionStatus(state.port, 0, 0, nullptr);
        if (posted == FALSE)
        {
            return lastErrorCode();
        }
        return SocketCode::SUCCESS;
    }

    namespace
    {
        bool isUsableCapacity(size_t completion_capacity)
        {
            return completion_capacity > 0 && completion_capacity <= static_cast<size_t>(ULONG_MAX);
        }

        SocketCode allocate(EventListenerState& state, size_t completion_capacity)
        {
            state.entries = new (std::nothrow) OVERLAPPED_ENTRY[completion_capacity];
            state.completions = new (std::nothrow) IoCompletion[completion_capacity];
            const bool is_allocated = state.entries != nullptr && state.completions != nullptr;
            if (!is_allocated) { return SocketCode::OUT_OF_RESOURCE; }
            return SocketCode::SUCCESS;
        }

        SocketCode checkAttach(const EventListenerState& state, const IoEvent& event,
            SocketHandle socket)
        {
            if (state.port == nullptr) { return SocketCode::CLOSED; }
            if (socket == INVALID_SOCKET_HANDLE) { return SocketCode::INVALID_ARGUMENT; }
            if (event.pending) { return SocketCode::BUSY; }
            if (attached(event)) { return SocketCode::INVALID_ARGUMENT; }
            return SocketCode::SUCCESS;
        }

        SocketCode associate(const EventListenerState& state, SOCKET native)
        {
            const HANDLE file = reinterpret_cast<HANDLE>(native);
            const HANDLE associated = ::CreateIoCompletionPort(file, state.port, 0, 0);
            if (associated == nullptr && ::GetLastError() != ERROR_INVALID_PARAMETER)
            {
                return lastErrorCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode lastErrorCode()
        {
            return toSocketCode(static_cast<int>(::GetLastError()));
        }
    }
}

#endif
