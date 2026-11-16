#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class TextKind : uint8_t
    {
        BINARY,
        UTF8,
        UTF8_WITH_CHARSET
    };
}
