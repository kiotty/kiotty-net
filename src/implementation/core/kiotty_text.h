#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstring>

namespace kiotty::net::implementation
{
    using TextResult = Result<Span<const char>, KiottyNetCode>;

    TextResult parseText(const char* text, size_t max_length);
    bool       equalsIgnoreCase(Span<const char> left, Span<const char> right);

    template <size_t N>
    Span<const char> textOf(const char (&field)[N])
    {
        const size_t length = strnlen(field, N);
        return Span<const char>{ field, length };
    }
}
