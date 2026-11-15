#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstring>

namespace kiotty::net::implementation
{
    using TextResult = Result<Span<const char>, KiottyNetCode>;

    inline constexpr char   CARRIAGE_RETURN = '\r';
    inline constexpr char   LINE_FEED = '\n';
    inline constexpr char   LINE_END[] = "\r\n";
    inline constexpr size_t LINE_END_BYTES = sizeof(LINE_END) - 1;

    TextResult parseText(const char* text, size_t max_length);
    bool       equalsIgnoreCase(Span<const char> left, Span<const char> right);
    bool       isUtf8(Span<const char> text);
    Span<const char> literalOf(const char* text);

    template <size_t N>
    Span<const char> textOf(const char (&field)[N])
    {
        const size_t length = strnlen(field, N);
        return Span<const char>{ field, length };
    }
}
