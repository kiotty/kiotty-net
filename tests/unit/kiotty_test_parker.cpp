#include "core/kiotty_parker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using kiotty::net::implementation::Parker;

namespace
{
    typedef std::chrono::steady_clock Clock;

    long long elapsedMilliseconds(Clock::time_point since)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
    }
}

TEST(Parker, ParkReturnsWhenUnparkMakesReadyTrue)
{
    Parker parker;
    bool finished = false;
    int code = 0;
    std::thread other([&parker, &finished, &code]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        parker.unpark([&finished, &code]() {
            code = 7;
            finished = true;
        });
    });
    bool ready = parker.park([&finished]() { return finished; }, 5000);
    other.join();
    EXPECT_TRUE(ready);
    EXPECT_EQ(code, 7);
}

TEST(Parker, ParkWithTimeoutReturnsFalseWhenNothingChanges)
{
    Parker parker;
    bool finished = false;
    Clock::time_point start = Clock::now();
    bool ready = parker.park([&finished]() { return finished; }, 50);
    long long waited = elapsedMilliseconds(start);
    EXPECT_FALSE(ready);
    EXPECT_GE(waited, 45);
    EXPECT_LT(waited, 2000);
}

TEST(Parker, ParkWithTimeoutZeroWaitsUntilUnpark)
{
    Parker parker;
    bool finished = false;
    std::atomic<bool> returned(false);
    std::thread waiter([&parker, &finished, &returned]() {
        parker.park([&finished]() { return finished; }, 0);
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(returned.load());
    parker.unpark([&finished]() { finished = true; });
    waiter.join();
    EXPECT_TRUE(returned.load());
}

TEST(Parker, UnparkBeforeParkIsNotLost)
{
    Parker parker;
    bool finished = false;
    parker.unpark([&finished]() { finished = true; });
    Clock::time_point start = Clock::now();
    EXPECT_TRUE(parker.park([&finished]() { return finished; }, 5000));
    EXPECT_LT(elapsedMilliseconds(start), 1000);
}
