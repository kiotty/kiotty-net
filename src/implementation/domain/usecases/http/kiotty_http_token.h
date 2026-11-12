#pragma once

#include "core/kiotty_span.h"
#include "kiotty_net_base.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    bool             isTokenCharacter(char character);
    bool             isVisibleCharacter(char character);
    bool             isWhitespace(char character);
    bool             isToken(Span<const char> text);
    size_t           skipToken(Span<const char> text, size_t position);
    size_t           skipWhitespace(Span<const char> text, size_t position);
    size_t           findCharacter(Span<const char> text, char character, size_t position);
    Span<const char> trimWhitespace(Span<const char> text);
    Span<const char> spanOf(KiottyNetSpan span);
    bool             equalsLiteralIgnoreCase(Span<const char> text, const char* literal);
}
