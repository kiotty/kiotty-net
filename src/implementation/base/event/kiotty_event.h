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
            WORK_READY,
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
            static IoCompletion workReady(int32_t slot);
            static IoCompletion workDone(int32_t slot, IoAction outcome);
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

        inline IoCompletion IoCompletion::workReady(int32_t slot)
        {
            IoCompletion completion = { IoKind::WORK_READY, slot, SocketCode::SUCCESS, 0, IoAction::none() };
            return completion;
        }

        inline IoCompletion IoCompletion::workDone(int32_t slot, IoAction outcome)
        {
            IoCompletion completion = { IoKind::WORK_DONE, slot, SocketCode::SUCCESS, 0, outcome };
            return completion;
        }
    }
}

#endif
