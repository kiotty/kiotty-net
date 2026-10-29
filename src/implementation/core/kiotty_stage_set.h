#if !defined(KIOTTY_STAGE_SET_H)
#define KIOTTY_STAGE_SET_H

#include "core/kiotty_stage.h"

#include <cstddef>

namespace kiotty
{
    struct StageSet
    {
        StageSet(size_t receive_capacity, size_t send_capacity, size_t notify_capacity);

        bool valid() const;

        Stage receive;
        Stage send;
        Stage notify;
    };

    inline StageSet::StageSet(size_t receive_capacity, size_t send_capacity, size_t notify_capacity)
        : receive(receive_capacity)
        , send(send_capacity)
        , notify(notify_capacity)
    {
    }

    inline bool StageSet::valid() const
    {
        return receive.valid() && send.valid() && notify.valid();
    }
}

#endif
