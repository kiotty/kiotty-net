#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_READINESS

#include "data/infra/event/backend/kiotty_readiness_event_state.h"

#include <unistd.h>

namespace kiotty::net::implementation
{
    using backend::disarm;

    namespace
    {
        IoKind kindOf(const IoAction& action);
    }

    IoEvent::~IoEvent()
    {
        if (queued)
        {
            backend::unlinkReady(*listener, *this);
        }
        if (!backend::attached(*this))
        {
            return;
        }
        if (registered && backend::belongsTo(*this, *listener))
        {
            backend::unwatch(*listener, *this);
        }
        ::close(fd);
    }

    void backend::queueReady(EventListenerState& state, IoEvent& event, SocketCode code)
    {
        if (event.queued)
        {
            return;
        }
        event.result = code;
        event.queued = true;
        event.next = nullptr;
        if (state.ready_tail == nullptr)
        {
            state.ready_head = &event;
        }
        else
        {
            state.ready_tail->next = &event;
        }
        state.ready_tail = &event;
    }

    void backend::unlinkReady(EventListenerState& state, IoEvent& event)
    {
        IoEvent* previous = nullptr;
        IoEvent* current = state.ready_head;
        while (current != nullptr && current != &event)
        {
            previous = current;
            current = current->next;
        }
        if (current == nullptr)
        {
            return;
        }
        if (previous == nullptr)
        {
            state.ready_head = event.next;
        }
        else
        {
            previous->next = event.next;
        }
        if (state.ready_tail == &event)
        {
            state.ready_tail = previous;
        }
        event.next = nullptr;
        event.queued = false;
    }

    void backend::cancelReady(EventListenerState& state)
    {
        while (state.ready_head != nullptr)
        {
            IoEvent* event = state.ready_head;
            state.ready_head = event->next;
            event->next = nullptr;
            event->queued = false;
            disarm(*event);
        }
        state.ready_tail = nullptr;
    }

    size_t backend::drainReady(EventListenerState& state)
    {
        size_t count = 0;
        while (state.ready_head != nullptr && count < state.capacity)
        {
            IoEvent& event = *state.ready_head;
            state.ready_head = event.next;
            if (state.ready_head == nullptr)
            {
                state.ready_tail = nullptr;
            }
            event.next = nullptr;
            event.queued = false;
            state.completions[count] = completionOf(event, event.result);
            count += 1;
        }
        return count;
    }

    IoCompletion backend::completionOf(IoEvent& event, SocketCode code)
    {
        const IoKind kind = kindOf(event.action);
        IoCompletion completion = IoCompletion::of(kind, event.slot, event.action);
        completion.code = code;
        completion.length = event.done;
        disarm(event);
        return completion;
    }

    namespace
    {
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
    }
}

#endif
