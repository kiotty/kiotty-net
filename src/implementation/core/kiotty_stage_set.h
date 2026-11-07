#pragma once

#include "core/kiotty_stage.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    template <size_t Receive, size_t Send, size_t Notify>
    struct StageSet
    {
        void limit(size_t receive_capacity, size_t send_capacity, size_t notify_capacity);
        void clear();

        Stage<Receive> receive;
        Stage<Send>    send;
        Stage<Notify>  notify;
    };

    template <size_t Receive, size_t Send, size_t Notify>
    void StageSet<Receive, Send, Notify>::limit(size_t receive_capacity, size_t send_capacity, size_t notify_capacity)
    {
        receive.limit(receive_capacity);
        send.limit(send_capacity);
        notify.limit(notify_capacity);
    }

    template <size_t Receive, size_t Send, size_t Notify>
    void StageSet<Receive, Send, Notify>::clear()
    {
        receive.clear();
        send.clear();
        notify.clear();
    }
}
