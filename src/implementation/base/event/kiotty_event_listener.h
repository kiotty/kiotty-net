#if !defined(KIOTTY_EVENT_LISTENER_H)
#define KIOTTY_EVENT_LISTENER_H

#include "base/event/kiotty_event.h"
#include "base/socket/kiotty_socket.h"
#include "core/kiotty_span.h"

#if defined(_WIN32)
#include "base/event/backend/kiotty_iocp_event.h"
#elif defined(__linux__)
#include "base/event/backend/kiotty_epoll_event.h"
#else
#include "base/event/backend/kiotty_no_event.h"
#endif

#include <cstddef>
#include <cstdint>

#if defined(_WIN32) || defined(__linux__)
#define KIOTTY_NET_HAS_EVENT_BACKEND 1
#else
#define KIOTTY_NET_HAS_EVENT_BACKEND 0
#endif

namespace kiotty::net::implementation
{
    struct AcceptBuffer
    {
        static const size_t ADDRESS_LENGTH = 128 + 16;

        SocketHandle socket;
        char         addresses[2 * ADDRESS_LENGTH];
    };

    class EventListener
    {
    public:
        static const size_t   STORAGE_SIZE = 256;
        static const size_t   STORAGE_ALIGN = 16;
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
        alignas(STORAGE_ALIGN) unsigned char _storage[STORAGE_SIZE];
    };
}

#endif
