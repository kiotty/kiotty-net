#pragma once

#include "http/domain/entity/kiotty_http_request_line.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr int32_t FIRST_STATUS = 100;
    inline constexpr int32_t LAST_STATUS = 599;

    struct StatusLine
    {
        int32_t     status;
        HttpVersion version;
    };

    static_assert(sizeof(StatusLine) == 2 * sizeof(int32_t), "StatusLine: unexpected padding");
}
