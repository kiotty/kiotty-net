#if !defined(KIOTTY_EVENT_H)
#define KIOTTY_EVENT_H

#include "base/error/kiotty_socket_error.h"
#include "base/event/kiotty_io_action.h"

#include <cstddef>
#include <cstdint>

namespace kiotty
{
    namespace net
    {
        struct IoPayload
        {
            void*  buffer;
            size_t length;
        };

        enum class IoKind : uint8_t
        {
            ACCEPT,
            RECEIVE,
            SEND,
            WAKE,
            TIMEOUT,
            WORK_DONE
        };

        struct IoCompletion
        {
            IoKind     kind;
            int32_t    slot;
            SocketCode code;
            size_t     length;
            IoAction   action;

            static IoCompletion wake();
            static IoCompletion timeout(int32_t slot);
            static IoCompletion workDone(int32_t slot);
        };

        class IoEvent
        {
        public:
            static const size_t HOLDER_SIZE = 128;
            static const size_t HOLDER_ALIGN = 8;

            bool            pending() const;
            const IoAction& action() const;

        protected:
            IoEvent();

            bool     _pending;
            IoAction _action;
        };

        inline IoCompletion IoCompletion::wake()
        {
            IoCompletion completion = { IoKind::WAKE, -1, SocketCode::SUCCESS, 0, IoAction::none() };
            return completion;
        }

        inline IoCompletion IoCompletion::timeout(int32_t slot)
        {
            IoCompletion completion = { IoKind::TIMEOUT, slot, SocketCode::SUCCESS, 0, IoAction::none() };
            return completion;
        }

        inline IoCompletion IoCompletion::workDone(int32_t slot)
        {
            IoCompletion completion = { IoKind::WORK_DONE, slot, SocketCode::SUCCESS, 0, IoAction::none() };
            return completion;
        }

        inline IoEvent::IoEvent()
            : _pending(false)
            , _action(IoAction::none())
        {
        }

        inline bool IoEvent::pending() const
        {
            return _pending;
        }

        inline const IoAction& IoEvent::action() const
        {
            return _action;
        }
    }
}

#endif
