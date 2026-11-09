#if defined(__linux__)

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_epoll_event_state.h"

#include <sys/epoll.h>
#include <sys/socket.h>

#include <cerrno>

namespace kiotty::net::implementation
{
    using backend::attached;
    using backend::belongsTo;
    using backend::queueReady;
    using backend::sendRest;
    using backend::watch;

    namespace
    {
        SocketCode admit(const EventListenerState& state, const IoEvent& event,
            const IoAction& action, IoPayload payload);
        SocketCode checkRequest(const IoAction& action, IoPayload payload);
        void       arm(IoEvent& event, const IoAction& action, IoPayload request);
        uint32_t   interestOf(const IoAction& action);
        int        sendFlags();
        bool       isRetryable(ssize_t sent);
        bool       isWouldBlock(ssize_t sent);
    }

    SocketCode EventListener::submit(IoEvent& event, const IoAction& action, IoPayload payload)
    {
        EventListenerState& state = _state;
        const SocketCode admitted = admit(state, event, action, payload);
        if (admitted != SocketCode::SUCCESS) { return admitted; }
        arm(event, action, payload);
        SocketCode started = SocketCode::WOULD_BLOCK;
        if (action.kind == IoActionKind::WRITE) { started = sendRest(event); }
        if (started != SocketCode::WOULD_BLOCK)
        {
            queueReady(state, event, started);
            return SocketCode::SUCCESS;
        }
        const SocketCode watched = watch(state, event);
        if (watched != SocketCode::SUCCESS) { queueReady(state, event, watched); }
        return SocketCode::SUCCESS;
    }

    SocketCode backend::watch(EventListenerState& state, IoEvent& event)
    {
        epoll_event interest{};
        interest.events = interestOf(event.action);
        interest.data.ptr = &event;
        int operation = EPOLL_CTL_ADD;
        if (event.registered)
        {
            operation = EPOLL_CTL_MOD;
        }
        const int controlled = ::epoll_ctl(state.epoll, operation, event.fd, &interest);
        if (controlled != 0)
        {
            return lastSocketCode();
        }
        event.registered = true;
        return SocketCode::SUCCESS;
    }

    SocketCode backend::sendRest(IoEvent& event)
    {
        const char* bytes = static_cast<const char*>(event.payload.buffer);
        while (event.done < event.payload.length)
        {
            const size_t rest = event.payload.length - event.done;
            const ssize_t sent = ::send(event.fd, bytes + event.done, rest, sendFlags());
            if (sent > 0)
            {
                event.done += static_cast<size_t>(sent);
                continue;
            }
            if (isRetryable(sent))
            {
                continue;
            }
            if (isWouldBlock(sent))
            {
                return SocketCode::WOULD_BLOCK;
            }
            return lastSocketCode();
        }
        return SocketCode::SUCCESS;
    }

    namespace
    {
        SocketCode admit(const EventListenerState& state, const IoEvent& event,
            const IoAction& action, IoPayload payload)
        {
            if (state.epoll < 0) { return SocketCode::CLOSED; }
            const bool is_owned = attached(event) && belongsTo(event, state);
            if (!is_owned) { return SocketCode::INVALID_ARGUMENT; }
            if (event.pending) { return SocketCode::BUSY; }
            return checkRequest(action, payload);
        }

        SocketCode checkRequest(const IoAction& action, IoPayload payload)
        {
            switch (action.kind)
            {
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            case IoActionKind::WRITE:
                if (payload.buffer == nullptr || payload.length == 0)
                {
                    return SocketCode::INVALID_ARGUMENT;
                }
                return SocketCode::SUCCESS;
            case IoActionKind::ACCEPT:
                return SocketCode::SUCCESS;
            case IoActionKind::NONE:
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return SocketCode::INVALID_ARGUMENT;
            }
            return SocketCode::INVALID_ARGUMENT;
        }

        void arm(IoEvent& event, const IoAction& action, IoPayload request)
        {
            event.pending = true;
            event.action = action;
            event.payload = request;
            event.done = 0;
            event.result = SocketCode::SUCCESS;
        }

        uint32_t interestOf(const IoAction& action)
        {
            if (action.kind == IoActionKind::WRITE)
            {
                return EPOLLOUT | EPOLLONESHOT;
            }
            return EPOLLIN | EPOLLONESHOT;
        }

        int sendFlags()
        {
#if defined(MSG_NOSIGNAL)
            return MSG_NOSIGNAL;
#else
            return 0;
#endif
        }

        bool isRetryable(ssize_t sent)
        {
            return sent < 0 && errno == EINTR;
        }

        bool isWouldBlock(ssize_t sent)
        {
            return sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
        }
    }
}

#endif
