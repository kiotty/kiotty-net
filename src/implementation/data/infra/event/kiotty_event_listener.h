#pragma once

#include "domain/entity/kiotty_io_completion.h"
#include "domain/entity/kiotty_io_payload.h"
#include "data/infra/socket/kiotty_passive_socket.h"
#include "data/infra/socket/kiotty_socket_transfer.h"
#include "core/kiotty_span.h"

#include "data/infra/event/backend/kiotty_event_backend.h"

#if KIOTTY_NET_EVENT_IOCP
#include "data/infra/event/backend/kiotty_iocp_event.h"
#elif KIOTTY_NET_EVENT_EPOLL
#include "data/infra/event/backend/kiotty_epoll_event.h"
#elif KIOTTY_NET_EVENT_KQUEUE
#include "data/infra/event/backend/kiotty_kqueue_event.h"
#else
#include "data/infra/event/backend/kiotty_no_event.h"
#endif

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct AcceptBuffer
    {
        static constexpr size_t SOCKET_ADDRESS_BYTES = 128;
        static constexpr size_t ACCEPT_ADDRESS_PADDING_BYTES = 16;
        static constexpr size_t ADDRESS_LENGTH =
            SOCKET_ADDRESS_BYTES + ACCEPT_ADDRESS_PADDING_BYTES;

        SocketHandle socket;
        char         addresses[2 * ADDRESS_LENGTH];
    };

    class EventListener
    {
    public:
        static const uint32_t WAIT_FOREVER = 0xFFFFFFFFu;

        EventListener();
        ~EventListener();

        EventListener(const EventListener&) = delete;
        EventListener& operator=(const EventListener&) = delete;

        SocketCode               open(size_t completion_capacity);
        void                     close();
        SocketCode               attach(IoEvent& event, SocketHandle socket, int32_t slot);
        SocketCode               submit(IoEvent& event, const IoAction& action, IoPayload payload);
        void                     detach(IoEvent& event);
        Span<const IoCompletion> wait(uint32_t timeout_milliseconds);
        SocketCode               wake();

    private:
        EventListenerState _state;
    };
}
