#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class TlsStep : uint8_t
    {
        SEND,
        RECEIVE,
        DONE,
        FAILED
    };
}
