#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    class HttpSharedHeaders
    {
    public:
        HttpSharedHeaders(Span<char> text, Span<KiottyNetHttpHeader> entries);

        KiottyNetCode                   set(Span<const char> name, Span<const char> value);
        KiottyNetCode                   remove(Span<const char> name);
        void                            clear();
        Span<const KiottyNetHttpHeader> list() const;

    private:
        size_t        findIndex(Span<const char> name) const;
        KiottyNetCode checkRoom(size_t index, size_t bytes) const;
        void          removeAt(size_t index);
        void          append(Span<const char> name, Span<const char> value);

        Span<char>                _text;
        Span<KiottyNetHttpHeader> _entries;
        size_t                    _text_length;
        size_t                    _count;
    };
}
