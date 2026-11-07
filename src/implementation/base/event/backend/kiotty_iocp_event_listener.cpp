#if defined(_WIN32)

#include "data/infra/error/kiotty_socket_error.h"
#include "base/event/kiotty_event_listener.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>
#include <winternl.h>

#include <chrono>
#include <climits>
#include <cstring>
#include <new>

namespace kiotty::net::implementation
{
    struct EventListenerState
    {
        HANDLE            port;
        OVERLAPPED_ENTRY* entries;
        IoCompletion*     completions;
        size_t            capacity;
        uint32_t          generation;
    };

    namespace
    {
        static_assert(sizeof(EventListenerState) <= EventListener::STORAGE_SIZE, "EventListenerState outgrew EventListener::STORAGE_SIZE");
        static_assert(alignof(EventListenerState) <= EventListener::STORAGE_ALIGN, "EventListenerState needs stricter alignment than EventListener::STORAGE_ALIGN");
        static_assert(sizeof(sockaddr_storage) + 16 == AcceptBuffer::ADDRESS_LENGTH, "AcceptEx needs sizeof(sockaddr_storage) + 16 per address");

        EventListenerState& stateOf(void* storage)
        {
            return *static_cast<EventListenerState*>(storage);
        }

        void arm(IoEvent& event, const IoAction& action)
        {
            event.pending = true;
            event.action = action;
            event.failure = SocketCode::SUCCESS;
            std::memset(&event.request.overlapped, 0, sizeof(event.request.overlapped));
            event.request.owner = &event;
        }

        void disarm(IoEvent& event)
        {
            event.pending = false;
        }

        bool attached(const IoEvent& event)
        {
            return event.socket != INVALID_SOCKET;
        }

        bool belongsTo(const IoEvent& event, const EventListenerState& state)
        {
            return event.listener == &state && event.generation == state.generation;
        }

        ULONG clampLength(size_t length)
        {
            if (length > static_cast<size_t>(ULONG_MAX))
            {
                return ULONG_MAX;
            }
            return static_cast<ULONG>(length);
        }

        SocketCode lastErrorCode()
        {
            return toSocketCode(static_cast<int>(::GetLastError()));
        }

        SocketCode checkRequest(const IoAction& action, IoPayload payload)
        {
            if (payload.buffer == nullptr)
            {
                return SocketCode::INVALID_ARGUMENT;
            }
            switch (action.kind)
            {
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::WRITE:
                if (payload.length == 0)
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                return SocketCode::SUCCESS;
            case IoActionKind::ACCEPT:
                if (payload.length != sizeof(AcceptBuffer))
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                if (static_cast<const AcceptBuffer*>(payload.buffer)->socket == INVALID_SOCKET_HANDLE)
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                return SocketCode::SUCCESS;
            default:
                return SocketCode::INVALID_ARGUMENT;
            }
        }

        SocketCode loadAcceptEx(IoEvent& event)
        {
            if (event.accept_ex != nullptr)
            {
                return SocketCode::SUCCESS;
            }
            GUID guid = WSAID_ACCEPTEX;
            DWORD returned = 0;
            int loaded = ::WSAIoctl(event.socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                &event.accept_ex, sizeof(event.accept_ex), &returned, nullptr, nullptr);
            if (loaded != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode startReceive(IoEvent& event, IoPayload payload)
        {
            WSABUF buffer;
            buffer.len = clampLength(payload.length);
            buffer.buf = static_cast<char*>(payload.buffer);
            DWORD flags = 0;
            int started = ::WSARecv(event.socket, &buffer, 1, nullptr, &flags, &event.request.overlapped, nullptr);
            if (started != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode startSend(IoEvent& event, IoPayload payload)
        {
            WSABUF buffer;
            buffer.len = clampLength(payload.length);
            buffer.buf = static_cast<char*>(payload.buffer);
            int started = ::WSASend(event.socket, &buffer, 1, nullptr, 0, &event.request.overlapped, nullptr);
            if (started != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode startAccept(IoEvent& event, IoPayload payload)
        {
            SocketCode loaded = loadAcceptEx(event);
            if (loaded != SocketCode::SUCCESS)
            {
                return loaded;
            }
            AcceptBuffer* buffer = static_cast<AcceptBuffer*>(payload.buffer);
            DWORD received = 0;
            DWORD address_length = static_cast<DWORD>(AcceptBuffer::ADDRESS_LENGTH);
            BOOL accepted = event.accept_ex(event.socket, static_cast<SOCKET>(buffer->socket), buffer->addresses, 0,
                address_length, address_length, &received, &event.request.overlapped);
            if (accepted == FALSE)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode start(IoEvent& event, const IoAction& action, IoPayload payload)
        {
            switch (action.kind)
            {
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
                return startReceive(event, payload);
            case IoActionKind::WRITE:
                return startSend(event, payload);
            case IoActionKind::ACCEPT:
                return startAccept(event, payload);
            default:
                return SocketCode::INVALID_ARGUMENT;
            }
        }

        SocketCode postFailure(EventListenerState& state, IoEvent& event, SocketCode failure)
        {
            event.failure = failure;
            BOOL posted = ::PostQueuedCompletionStatus(state.port, 0, 0, &event.request.overlapped);
            if (posted == FALSE)
            {
                disarm(event);
                return failure;
            }
            return SocketCode::SUCCESS;
        }

        IoKind kindOf(const IoAction& action)
        {
            switch (action.kind)
            {
            case IoActionKind::ACCEPT:
                return IoKind::ACCEPT;
            case IoActionKind::WRITE:
                return IoKind::SEND;
            default:
                return IoKind::RECEIVE;
            }
        }

        SocketCode statusOf(const IoEvent& event)
        {
            if (event.failure != SocketCode::SUCCESS)
            {
                return event.failure;
            }
            NTSTATUS status = static_cast<NTSTATUS>(event.request.overlapped.Internal);
            if (status >= 0)
            {
                return SocketCode::SUCCESS;
            }
            ULONG error = ::RtlNtStatusToDosError(status);
            return toSocketCode(static_cast<int>(error));
        }

        IoCompletion completionOf(const OVERLAPPED_ENTRY& entry)
        {
            if (entry.lpOverlapped == nullptr)
            {
                return IoCompletion::wake();
            }
            IocpRequest* request = reinterpret_cast<IocpRequest*>(entry.lpOverlapped);
            IoEvent& event = *request->owner;
            IoCompletion completion;
            completion.kind = kindOf(event.action);
            completion.slot = event.slot;
            completion.code = statusOf(event);
            completion.length = entry.dwNumberOfBytesTransferred;
            completion.action = event.action;
            if (completion.kind == IoKind::RECEIVE && completion.code == SocketCode::SUCCESS && completion.length == 0)
            {
                completion.code = SocketCode::CLOSED;
            }
            disarm(event);
            return completion;
        }

        DWORD remainingMilliseconds(std::chrono::steady_clock::time_point deadline)
        {
            std::chrono::steady_clock::duration left = deadline - std::chrono::steady_clock::now();
            if (left <= std::chrono::steady_clock::duration::zero())
            {
                return 0;
            }
            std::chrono::milliseconds whole = std::chrono::duration_cast<std::chrono::milliseconds>(left);
            if (whole < left)
            {
                whole += std::chrono::milliseconds(1);
            }
            return static_cast<DWORD>(whole.count());
        }

        bool dequeue(EventListenerState& state, uint32_t timeout_milliseconds, ULONG& removed)
        {
            std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_milliseconds);
            DWORD limit = timeout_milliseconds;
            for (;;)
            {
                BOOL dequeued = ::GetQueuedCompletionStatusEx(state.port, state.entries, static_cast<ULONG>(state.capacity), &removed, limit, FALSE);
                if (dequeued != FALSE)
                {
                    return true;
                }
                if (::GetLastError() != WAIT_TIMEOUT)
                {
                    return false;
                }
                if (timeout_milliseconds == EventListener::WAIT_FOREVER)
                {
                    continue;
                }
                limit = remainingMilliseconds(deadline);
                if (limit == 0)
                {
                    return false;
                }
            }
        }
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
        if (state.port != nullptr)
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        if (completion_capacity == 0 || completion_capacity > static_cast<size_t>(ULONG_MAX))
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        state.entries = new (std::nothrow) OVERLAPPED_ENTRY[completion_capacity];
        state.completions = new (std::nothrow) IoCompletion[completion_capacity];
        if (state.entries == nullptr || state.completions == nullptr)
        {
            close();
            return SocketCode::OUT_OF_RESOURCE;
        }
        state.port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (state.port == nullptr)
        {
            SocketCode failed = lastErrorCode();
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
        if (state.port == nullptr)
        {
            return SocketCode::CLOSED;
        }
        if (socket == INVALID_SOCKET_HANDLE)
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        if (event.pending)
        {
            return SocketCode::BUSY;
        }
        if (attached(event))
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        SOCKET native = static_cast<SOCKET>(socket);
        HANDLE associated = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(native), state.port, 0, 0);
        if (associated == nullptr && ::GetLastError() != ERROR_INVALID_PARAMETER)
        {
            return lastErrorCode();
        }
        event.listener = &state;
        event.generation = state.generation;
        event.socket = native;
        event.slot = slot;
        event.failure = SocketCode::SUCCESS;
        event.accept_ex = nullptr;
        return SocketCode::SUCCESS;
    }

    SocketCode EventListener::submit(IoEvent& event, const IoAction& action, IoPayload payload)
    {
        EventListenerState& state = stateOf(_storage);
        if (state.port == nullptr)
        {
            return SocketCode::CLOSED;
        }
        if (!attached(event) || !belongsTo(event, state))
        {
            return SocketCode::INVALID_ARGUMENT;
        }
        if (event.pending)
        {
            return SocketCode::BUSY;
        }
        SocketCode checked = checkRequest(action, payload);
        if (checked != SocketCode::SUCCESS)
        {
            return checked;
        }
        arm(event, action);
        SocketCode started = start(event, action, payload);
        if (started == SocketCode::SUCCESS || started == SocketCode::PENDING)
        {
            return SocketCode::SUCCESS;
        }
        return postFailure(state, event, started);
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

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_milliseconds)
    {
        EventListenerState& state = stateOf(_storage);
        Span<const IoCompletion> completions = { state.completions, 0 };
        if (state.port == nullptr)
        {
            return completions;
        }
        ULONG removed = 0;
        if (!dequeue(state, timeout_milliseconds, removed))
        {
            return completions;
        }
        for (ULONG i = 0; i < removed; ++i)
        {
            state.completions[i] = completionOf(state.entries[i]);
        }
        completions.length = removed;
        return completions;
    }

    SocketCode EventListener::wake()
    {
        EventListenerState& state = stateOf(_storage);
        if (state.port == nullptr)
        {
            return SocketCode::CLOSED;
        }
        BOOL posted = ::PostQueuedCompletionStatus(state.port, 0, 0, nullptr);
        if (posted == FALSE)
        {
            return lastErrorCode();
        }
        return SocketCode::SUCCESS;
    }
}

#endif
