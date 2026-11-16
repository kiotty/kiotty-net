#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class BodyKind : uint8_t
    {
        NONE,
        LENGTH,
        CHUNKED
    };

    enum class Persistence : uint8_t
    {
        CLOSE,
        KEEP_ALIVE,
        UPGRADE
    };

    struct BodyFraming
    {
        uint64_t length;
        BodyKind kind;
    };
}
