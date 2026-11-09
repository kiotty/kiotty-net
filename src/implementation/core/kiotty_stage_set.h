#pragma once

#include "core/kiotty_stage.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    struct StageSet
    {
        void reserve(size_t receive_capacity, size_t send_capacity, size_t notify_capacity);
        void limit(size_t receive_capacity, size_t send_capacity, size_t notify_capacity);
        void clear();

        Stage receive;
        Stage send;
        Stage notify;
    };

    inline void StageSet::reserve(size_t receive_capacity,
                                  size_t send_capacity,
                                  size_t notify_capacity)
    {
        receive.reserve(receive_capacity);
        send.reserve(send_capacity);
        notify.reserve(notify_capacity);
    }

    inline void StageSet::limit(size_t receive_capacity,
                                size_t send_capacity,
                                size_t notify_capacity)
    {
        receive.limit(receive_capacity);
        send.limit(send_capacity);
        notify.limit(notify_capacity);
    }

    inline void StageSet::clear()
    {
        receive.clear();
        send.clear();
        notify.clear();
    }
}
