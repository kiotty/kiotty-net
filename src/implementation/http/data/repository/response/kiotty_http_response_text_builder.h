#pragma once

#include "base/domain/entity/kiotty_calendar_time.h"
#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_response_text.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    bool        appendStatusLine(PrefixText& text, int32_t status);
    bool        appendField(PrefixText& text, Span<const char> name, Span<const char> value);
    bool        appendLengthField(PrefixText& text, uint64_t length);
    bool        appendDateField(PrefixText& text, const CalendarTime& time);
    AllowText   allowListOf(uint32_t methods);
    ChunkHead   chunkHeadOf(size_t length);
    DecimalText decimalOf(uint64_t value);
}
