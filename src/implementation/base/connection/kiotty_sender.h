#if !defined(KIOTTY_SENDER_H)
#define KIOTTY_SENDER_H

#include "core/kiotty_stage.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstring>

namespace kiotty::net::implementation
{
    struct Sender
    {
        static KiottyNetCode stage(StageBase& stage, KiottyNetSpan data);
        static void          release(StageBase& stage, size_t length);
    };

    inline KiottyNetCode Sender::stage(StageBase& stage, KiottyNetSpan data)
    {
        if (data.length == 0)
        {
            return KIOTTY_SUCCESS;
        }
        if (data.data == nullptr)
        {
            return KIOTTY_INVALID_ARGUMENT;
        }
        StageBase::Provider provider = stage.provider();
        if (provider.writable() < data.length)
        {
            return KIOTTY_BUFFER_FULL;
        }
        char* room = provider.prepare(data.length);
        std::memcpy(room, data.data, data.length);
        provider.commit(data.length);
        return KIOTTY_SUCCESS;
    }

    inline void Sender::release(StageBase& stage, size_t length)
    {
        stage.consumer().commit(length);
        stage.compact();
    }
}

#endif
