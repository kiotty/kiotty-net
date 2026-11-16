#pragma once

#include <cstdint>

namespace kiotty::net::implementation
{
    struct CalendarTime
    {
        int32_t year;
        int32_t month;
        int32_t day;
        int32_t weekday;
        int32_t hour;
        int32_t minute;
        int32_t second;
    };
}
