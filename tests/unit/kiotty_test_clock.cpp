#include "core/kiotty_clock.h"

#include <gtest/gtest.h>

TEST(Clock, MonotonicMsNeverDecreasesAcrossTenThousandCalls)
{
    uint64_t previous = kiotty::monotonicMs();
    for (int i = 0; i < 10000; ++i)
    {
        uint64_t now = kiotty::monotonicMs();
        ASSERT_GE(now, previous);
        previous = now;
    }
}
