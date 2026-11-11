#pragma once

#include "domain/entity/kiotty_io_action.h"
#include "domain/entity/kiotty_socket_code.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    enum class IoKind : uint8_t
    {
        ACCEPT,
        RECEIVE,
        SEND,
        WAKE,
        TIMEOUT,
        SEND_TIMEOUT,
        WORK_READY,
        WORK_DONE
    };

    struct IoCompletion
    {
        IoAction   action;
        size_t     length;
        int32_t    slot;
        IoKind     kind;
        SocketCode code;

        static IoCompletion of(IoKind kind, int32_t slot, const IoAction& action);
        static IoCompletion wake();
        static IoCompletion timeout(int32_t slot);
        static IoCompletion sendTimeout(int32_t slot);
        static IoCompletion workReady(int32_t slot);
        static IoCompletion workDone(int32_t slot, const IoAction& outcome);
    };

    static_assert(sizeof(IoCompletion) == sizeof(IoAction) + sizeof(size_t) + 8,
                  "IoCompletion: unexpected padding");

    inline IoCompletion IoCompletion::of(IoKind kind, int32_t slot, const IoAction& action)
    {
        return IoCompletion{ action, 0, slot, kind, SocketCode::SUCCESS };
    }

    inline IoCompletion IoCompletion::wake()
    {
        return of(IoKind::WAKE, -1, IoAction::none());
    }

    inline IoCompletion IoCompletion::timeout(int32_t slot)
    {
        return of(IoKind::TIMEOUT, slot, IoAction::none());
    }

    inline IoCompletion IoCompletion::sendTimeout(int32_t slot)
    {
        return of(IoKind::SEND_TIMEOUT, slot, IoAction::none());
    }

    inline IoCompletion IoCompletion::workReady(int32_t slot)
    {
        return of(IoKind::WORK_READY, slot, IoAction::none());
    }

    inline IoCompletion IoCompletion::workDone(int32_t slot, const IoAction& outcome)
    {
        return of(IoKind::WORK_DONE, slot, outcome);
    }
}
