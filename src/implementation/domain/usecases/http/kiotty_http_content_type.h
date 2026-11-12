#pragma once

#include "core/kiotty_span.h"
#include "core/kiotty_text.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class TextKind : uint8_t
    {
        BINARY,
        UTF8,
        UTF8_WITH_CHARSET
    };

    Span<const char>         contentTypeTextOf(KiottyNetHttpContentType type);
    TextKind                 textKindOf(KiottyNetHttpContentType type);
    KiottyNetHttpContentType contentTypeOfPath(Span<const char> path);
    TextResult               parseCustomContentType(const char* text);
}
