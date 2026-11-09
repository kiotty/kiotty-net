#if defined(_WIN32)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_iocp_event_state.h"

#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>

#include <climits>

namespace kiotty::net::implementation
{
    using backend::attached;
    using backend::belongsTo;
    using backend::disarm;

    namespace
    {
        SocketCode admit(const EventListenerState& state, const IoEvent& event,
            const IoAction& action, IoPayload payload);
        SocketCode checkRequest(const IoAction& action, IoPayload payload);
        SocketCode checkAccept(IoPayload payload);
        void       arm(IoEvent& event, const IoAction& action);
        SocketCode start(IoEvent& event, const IoAction& action, IoPayload payload);
        SocketCode startReceive(IoEvent& event, IoPayload payload);
        SocketCode startSend(IoEvent& event, IoPayload payload);
        SocketCode startAccept(IoEvent& event, IoPayload payload);
        SocketCode loadAcceptEx(IoEvent& event);
        SocketCode postFailure(const EventListenerState& state, IoEvent& event, SocketCode failure);
        WSABUF     bufferOf(IoPayload payload);
        ULONG      clampLength(size_t length);
    }

    SocketCode EventListener::submit(IoEvent& event, const IoAction& action, IoPayload payload)
    {
        EventListenerState& state = _state;
        const SocketCode admitted = admit(state, event, action, payload);
        if (admitted != SocketCode::SUCCESS) { return admitted; }
        arm(event, action);
        const SocketCode started = start(event, action, payload);
        if (started == SocketCode::SUCCESS || started == SocketCode::PENDING)
        {
            return SocketCode::SUCCESS;
        }
        return postFailure(state, event, started);
    }

    namespace
    {
        SocketCode admit(const EventListenerState& state, const IoEvent& event,
            const IoAction& action, IoPayload payload)
        {
            if (state.port == nullptr) { return SocketCode::CLOSED; }
            const bool is_owned = attached(event) && belongsTo(event, state);
            if (!is_owned) { return SocketCode::INVALID_ARGUMENT; }
            if (event.pending) { return SocketCode::BUSY; }
            return checkRequest(action, payload);
        }

        SocketCode checkRequest(const IoAction& action, IoPayload payload)
        {
            if (payload.buffer == nullptr) { return SocketCode::INVALID_ARGUMENT; }
            switch (action.kind)
            {
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::WRITE:
                if (payload.length == 0) { return SocketCode::INVALID_ARGUMENT; }
                return SocketCode::SUCCESS;
            case IoActionKind::ACCEPT:
                return checkAccept(payload);
            case IoActionKind::NONE:
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return SocketCode::INVALID_ARGUMENT;
            }
            return SocketCode::INVALID_ARGUMENT;
        }

        SocketCode checkAccept(IoPayload payload)
        {
            if (payload.length != sizeof(AcceptBuffer)) { return SocketCode::INVALID_ARGUMENT; }
            return SocketCode::SUCCESS;
        }

        void arm(IoEvent& event, const IoAction& action)
        {
            event.pending = true;
            event.action = action;
            event.failure = SocketCode::SUCCESS;
            event.request.overlapped = OVERLAPPED{};
            event.request.owner = &event;
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
            case IoActionKind::NONE:
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return SocketCode::INVALID_ARGUMENT;
            }
            return SocketCode::INVALID_ARGUMENT;
        }

        SocketCode startReceive(IoEvent& event, IoPayload payload)
        {
            WSABUF buffer = bufferOf(payload);
            DWORD flags = 0;
            OVERLAPPED* const overlapped = &event.request.overlapped;
            const int started =
                ::WSARecv(event.socket, &buffer, 1, nullptr, &flags, overlapped, nullptr);
            if (started != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode startSend(IoEvent& event, IoPayload payload)
        {
            WSABUF buffer = bufferOf(payload);
            OVERLAPPED* const overlapped = &event.request.overlapped;
            const int started =
                ::WSASend(event.socket, &buffer, 1, nullptr, 0, overlapped, nullptr);
            if (started != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode startAccept(IoEvent& event, IoPayload payload)
        {
            AcceptBuffer* const buffer = static_cast<AcceptBuffer*>(payload.buffer);
            if (buffer->socket == INVALID_SOCKET_HANDLE) { return SocketCode::INVALID_ARGUMENT; }
            const SocketCode loaded = loadAcceptEx(event);
            if (loaded != SocketCode::SUCCESS) { return loaded; }
            const SOCKET accepted_socket = static_cast<SOCKET>(buffer->socket);
            const DWORD address_length = static_cast<DWORD>(AcceptBuffer::ADDRESS_LENGTH);
            DWORD received = 0;
            const BOOL accepted = event.accept_ex(event.socket, accepted_socket,
                buffer->addresses, 0, address_length, address_length, &received,
                &event.request.overlapped);
            if (accepted == FALSE)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode loadAcceptEx(IoEvent& event)
        {
            if (event.accept_ex != nullptr)
            {
                return SocketCode::SUCCESS;
            }
            GUID guid = WSAID_ACCEPTEX;
            DWORD returned = 0;
            const int loaded = ::WSAIoctl(event.socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guid, sizeof(guid), &event.accept_ex, sizeof(event.accept_ex), &returned,
                nullptr, nullptr);
            if (loaded != 0)
            {
                return lastSocketCode();
            }
            return SocketCode::SUCCESS;
        }

        SocketCode postFailure(const EventListenerState& state, IoEvent& event, SocketCode failure)
        {
            event.failure = failure;
            const BOOL posted =
                ::PostQueuedCompletionStatus(state.port, 0, 0, &event.request.overlapped);
            if (posted == FALSE)
            {
                disarm(event);
                return failure;
            }
            return SocketCode::SUCCESS;
        }

        WSABUF bufferOf(IoPayload payload)
        {
            WSABUF buffer{};
            buffer.len = clampLength(payload.length);
            buffer.buf = static_cast<char*>(payload.buffer);
            return buffer;
        }

        ULONG clampLength(size_t length)
        {
            if (length > static_cast<size_t>(ULONG_MAX))
            {
                return ULONG_MAX;
            }
            return static_cast<ULONG>(length);
        }
    }
}

#endif
