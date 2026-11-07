#pragma once

#include <cstddef>

namespace kiotty::net::implementation
{
    struct IoPayload
    {
        void*  buffer;
        size_t length;
    };

    static_assert(sizeof(IoPayload) == sizeof(void*) + sizeof(size_t), "IoPayload: unexpected padding");
}
