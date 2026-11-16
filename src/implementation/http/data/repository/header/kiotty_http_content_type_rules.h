#pragma once

#include "core/kiotty_span.h"
#include "core/kiotty_text.h"
#include "http/domain/entity/kiotty_http_content_type.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    Span<const char>         contentTypeTextOf(KiottyNetHttpContentType type);
    TextKind                 textKindOf(KiottyNetHttpContentType type);
    KiottyNetHttpContentType contentTypeOfPath(Span<const char> path);
    TextResult               parseCustomContentType(const char* text);
}
