#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class HttpState : int32_t
    {
        REQUEST_LINE,
        HEADER_LINE,
        BODY,
        CHUNK_SIZE,
        CHUNK_DATA,
        CHUNK_END,
        TRAILER,
        CONTINUING,
        WORKING,
        SENDING,
        CLOSING,
        UPGRADED
    };
}
