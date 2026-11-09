#if defined(_WIN32)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_iocp_event_state.h"

#include <winsock2.h>
#include <windows.h>
#include <winternl.h>

#include <chrono>

namespace kiotty::net::implementation
{
    using backend::disarm;

    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Milliseconds = std::chrono::milliseconds;

        ULONG        dequeue(EventListenerState& state, uint32_t timeout_milliseconds);
        IoCompletion completionOf(const OVERLAPPED_ENTRY& entry);
        IoKind       kindOf(const IoAction& action);
        SocketCode   statusOf(const IoEvent& event);
        bool         isEndOfStream(const IoCompletion& completion);
        DWORD        remainingMilliseconds(Clock::time_point deadline);
    }

    Span<const IoCompletion> EventListener::wait(uint32_t timeout_milliseconds)
    {
        EventListenerState& state = _state;
        Span<const IoCompletion> completions = { state.completions.data(), 0 };
        if (state.port == nullptr)
        {
            return completions;
        }
        const ULONG removed = dequeue(state, timeout_milliseconds);
        for (ULONG i = 0; i < removed; ++i)
        {
            state.completions[i] = completionOf(state.entries[i]);
        }
        completions.length = removed;
        return completions;
    }

    namespace
    {
        ULONG dequeue(EventListenerState& state, uint32_t timeout_milliseconds)
        {
            const bool forever = timeout_milliseconds == EventListener::WAIT_FOREVER;
            const Clock::time_point deadline = Clock::now() + Milliseconds(timeout_milliseconds);
            const ULONG capacity = static_cast<ULONG>(state.capacity);
            DWORD limit = timeout_milliseconds;
            for (;;)
            {
                ULONG removed = 0;
                OVERLAPPED_ENTRY* const entries = state.entries.data();
                const BOOL dequeued = ::GetQueuedCompletionStatusEx(state.port, entries,
                    capacity, &removed, limit, FALSE);
                if (dequeued != FALSE) { return removed; }
                if (::GetLastError() != WAIT_TIMEOUT) { return 0; }
                if (forever) { continue; }
                limit = remainingMilliseconds(deadline);
                if (limit == 0) { return 0; }
            }
        }

        IoCompletion completionOf(const OVERLAPPED_ENTRY& entry)
        {
            if (entry.lpOverlapped == nullptr)
            {
                return IoCompletion::wake();
            }
            const IocpRequest* request = reinterpret_cast<IocpRequest*>(entry.lpOverlapped);
            IoEvent& event = *request->owner;
            const IoKind kind = kindOf(event.action);
            IoCompletion completion = IoCompletion::of(kind, event.slot, event.action);
            completion.code = statusOf(event);
            completion.length = entry.dwNumberOfBytesTransferred;
            if (isEndOfStream(completion))
            {
                completion.code = SocketCode::CLOSED;
            }
            disarm(event);
            return completion;
        }

        IoKind kindOf(const IoAction& action)
        {
            switch (action.kind)
            {
            case IoActionKind::ACCEPT:
                return IoKind::ACCEPT;
            case IoActionKind::WRITE:
                return IoKind::SEND;
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::NONE:
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return IoKind::RECEIVE;
            }
            return IoKind::RECEIVE;
        }

        SocketCode statusOf(const IoEvent& event)
        {
            if (event.failure != SocketCode::SUCCESS)
            {
                return event.failure;
            }
            const NTSTATUS status = static_cast<NTSTATUS>(event.request.overlapped.Internal);
            if (status >= 0)
            {
                return SocketCode::SUCCESS;
            }
            const ULONG error = ::RtlNtStatusToDosError(status);
            return toSocketCode(static_cast<int>(error));
        }

        bool isEndOfStream(const IoCompletion& completion)
        {
            const bool is_receive = completion.kind == IoKind::RECEIVE;
            const bool is_success = completion.code == SocketCode::SUCCESS;
            return is_receive && is_success && completion.length == 0;
        }

        DWORD remainingMilliseconds(Clock::time_point deadline)
        {
            const Clock::duration left = deadline - Clock::now();
            if (left <= Clock::duration::zero())
            {
                return 0;
            }
            Milliseconds whole = std::chrono::duration_cast<Milliseconds>(left);
            if (whole < left)
            {
                whole += Milliseconds(1);
            }
            return static_cast<DWORD>(whole.count());
        }
    }
}

#endif
