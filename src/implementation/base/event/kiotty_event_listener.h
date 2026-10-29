#if !defined(KIOTTY_EVENT_LISTENER_H)
#define KIOTTY_EVENT_LISTENER_H

#include "base/event/kiotty_event.h"
#include "base/socket/kiotty_socket.h"
#include "core/kiotty_holder.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        struct AcceptBuffer
        {
            static const size_t ADDRESS_LENGTH = 128 + 16;

            SocketHandle socket;
            char         addresses[2 * ADDRESS_LENGTH];
        };

        typedef Result<SocketCode, Holder<IoEvent>> AttachResult;

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
            AttachResult             attach(SocketHandle socket, int32_t slot);
            SocketCode               submit(Holder<IoEvent>& event, const IoAction& action, IoPayload payload);
            void                     detach(Holder<IoEvent>& event);
            Span<const IoCompletion> wait(uint32_t timeout_ms);
            SocketCode               wake();

        private:
            alignas(STORAGE_ALIGN) unsigned char _storage[STORAGE_SIZE];
        };
    }
}

#endif
