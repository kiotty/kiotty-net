#if !defined(KIOTTY_TEST_WAIT_H)
#define KIOTTY_TEST_WAIT_H

#include <chrono>
#include <cstdint>
#include <thread>

namespace kiotty_test
{
    template <typename Predicate>
    bool waitUntil(Predicate predicate, uint32_t timeout_ms)
    {
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;)
        {
            if (predicate())
            {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    inline uint32_t elapsedMs(std::chrono::steady_clock::time_point since)
    {
        std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - since;
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
}

#endif
