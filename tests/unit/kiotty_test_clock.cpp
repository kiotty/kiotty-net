#include "core/kiotty_clock.h"

#include <gtest/gtest.h>

TEST(Clock, MonotonicMsNeverDecreasesAcrossTenThousandCalls)
{
    uint64_t previous = kiotty::net::implementation::monotonicMilliseconds();
    for (int i = 0; i < 10000; ++i)
    {
        uint64_t now = kiotty::net::implementation::monotonicMilliseconds();
        ASSERT_GE(now, previous);
        previous = now;
    }
}
