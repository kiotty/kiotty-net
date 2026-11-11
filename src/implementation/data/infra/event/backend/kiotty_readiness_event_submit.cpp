#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_READINESS

#include "data/infra/error/kiotty_socket_error.h"
#include "data/infra/event/backend/kiotty_readiness_event_state.h"

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
        void       arm(IoEvent& event, const IoAction& action, IoPayload request);
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

    SocketCode backend::sendRest(IoEvent& event)
    {
        while (event.done < event.payload.total())
        {
            const msghdr* const message = restMessage(event);
            const ssize_t sent = ::sendmsg(event.fd, message, sendFlags());
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

    msghdr* backend::restMessage(IoEvent& event)
    {
        const IoPiece first = event.payload.first;
        const IoPiece second = event.payload.second;
        size_t count = 0;
        if (event.done < first.length)
        {
            char* const bytes = static_cast<char*>(first.buffer) + event.done;
            event.vectors[count] = iovec{ bytes, first.length - event.done };
            count += 1;
        }
        const size_t second_done = event.done > first.length ? event.done - first.length : 0;
        if (second_done < second.length)
        {
            char* const bytes = static_cast<char*>(second.buffer) + second_done;
            event.vectors[count] = iovec{ bytes, second.length - second_done };
            count += 1;
        }
        event.message = msghdr{};
        event.message.msg_iov = event.vectors;
        event.message.msg_iovlen = static_cast<decltype(event.message.msg_iovlen)>(count);
        return &event.message;
    }

    SocketCode backend::checkRequest(const IoAction& action, IoPayload payload)
    {
        switch (action.kind)
        {
        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
        case IoActionKind::WRITE:
            if (payload.first.buffer == nullptr || payload.first.length == 0)
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

    namespace
    {
        SocketCode admit(const EventListenerState& state, const IoEvent& event,
            const IoAction& action, IoPayload payload)
        {
            if (!backend::isOpen(state)) { return SocketCode::CLOSED; }
            const bool is_owned = attached(event) && belongsTo(event, state);
            if (!is_owned) { return SocketCode::INVALID_ARGUMENT; }
            if (event.pending) { return SocketCode::BUSY; }
            return backend::checkRequest(action, payload);
        }

        void arm(IoEvent& event, const IoAction& action, IoPayload request)
        {
            event.pending = true;
            event.action = action;
            event.payload = request;
            event.done = 0;
            event.result = SocketCode::SUCCESS;
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
