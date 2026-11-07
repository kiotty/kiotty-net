#pragma once

#include "core/kiotty_span.h"
#include "core/kiotty_stage.h"
#include "kiotty_net_base.h"

#include <cstddef>

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
            return KIOTTY_NET_CODE_SUCCESS;
        }
        if (data.data == nullptr) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        StageBase::Provider provider = stage.provider();
        const size_t writable = provider.writable();
        if (writable < data.length) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const Span<char> room{ provider.prepare(data.length), data.length };
        const bool copied = room.copyFrom(Span<const char>{ data.data, data.length });
        if (!copied) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        provider.commit(data.length);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    inline void Sender::release(StageBase& stage, size_t length)
    {
        stage.consumer().commit(length);
        stage.compact();
    }
}
