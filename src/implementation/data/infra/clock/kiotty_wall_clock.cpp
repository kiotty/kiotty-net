#include "data/infra/clock/kiotty_wall_clock.h"

#include <ctime>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t TM_YEAR_ORIGIN = 1900;
        constexpr int32_t TM_MONTH_ORIGIN = 1;

        std::tm splitUtc(std::time_t now);
    }

    CalendarTime currentUtcTime()
    {
        const std::time_t now = std::time(nullptr);
        const std::tm parts = splitUtc(now);
        return CalendarTime{ parts.tm_year + TM_YEAR_ORIGIN,
                             parts.tm_mon + TM_MONTH_ORIGIN,
                             parts.tm_mday,
                             parts.tm_wday,
                             parts.tm_hour,
                             parts.tm_min,
                             parts.tm_sec };
    }

    namespace
    {
        std::tm splitUtc(std::time_t now)
        {
            std::tm parts{};
#if defined(_WIN32)
            (void)gmtime_s(&parts, &now);
#else
            (void)gmtime_r(&now, &parts);
#endif
            return parts;
        }
    }
}
