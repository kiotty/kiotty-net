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
        static KiottyNetCode stage(StageBase& stage, Span<const KiottyNetSpan> pieces);
        static void          release(StageBase& stage, size_t length);
    };

    inline KiottyNetCode Sender::stage(StageBase& stage, KiottyNetSpan data)
    {
        return Sender::stage(stage, Span<const KiottyNetSpan>{ &data, 1 });
    }

    inline KiottyNetCode Sender::stage(StageBase& stage, Span<const KiottyNetSpan> pieces)
    {
        size_t total = 0;
        for (const KiottyNetSpan& piece : pieces)
        {
            if (piece.data == nullptr && piece.length > 0) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
            total += piece.length;
        }
        if (total == 0)
        {
            return KIOTTY_NET_CODE_SUCCESS;
        }
        StageBase::Provider provider = stage.provider();
        const size_t writable = provider.writable();
        if (writable < total) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const Span<char> room{ provider.prepare(total), total };
        size_t offset = 0;
        for (const KiottyNetSpan& piece : pieces)
        {
            const Span<char> target = room.skip(offset);
            const bool copied = target.copyFrom(Span<const char>{ piece.data, piece.length });
            if (!copied) { return KIOTTY_NET_CODE_BUFFER_FULL; }
            offset += piece.length;
        }
        provider.commit(total);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    inline void Sender::release(StageBase& stage, size_t length)
    {
        stage.consumer().commit(length);
        stage.compact();
    }
}
