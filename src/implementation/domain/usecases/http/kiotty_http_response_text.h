#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"
#include "domain/entity/kiotty_calendar_time.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t RESPONSE_PREFIX_BYTES = 256;
    inline constexpr size_t ALLOW_TEXT_BYTES = 64;

    using PrefixText = FixedString<RESPONSE_PREFIX_BYTES + 1>;
    using AllowText = FixedString<ALLOW_TEXT_BYTES>;

    bool      appendStatusLine(PrefixText& text, int32_t status);
    bool      appendField(PrefixText& text, Span<const char> name, Span<const char> value);
    bool      appendLengthField(PrefixText& text, uint64_t length);
    bool      appendDateField(PrefixText& text, const CalendarTime& time);
    AllowText allowListOf(uint32_t methods);
}
