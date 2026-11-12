#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_span.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    inline constexpr size_t WEB_SOCKET_KEY_LENGTH = 24;
    inline constexpr size_t WEB_SOCKET_ACCEPT_LENGTH = 28;

    using AcceptText = FixedString<WEB_SOCKET_ACCEPT_LENGTH + 1>;

    bool       isWebSocketKey(Span<const char> key);
    AcceptText acceptOf(Span<const char> key);
}
