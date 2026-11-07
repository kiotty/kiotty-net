#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace kiotty::net::implementation
{
    class Parker
    {
    public:
        static constexpr uint64_t MAX_SLICE_MILLISECONDS = 86400000;

        Parker() = default;
        Parker(const Parker&) = delete;
        Parker& operator=(const Parker&) = delete;

        template <class Ready>
        bool park(Ready ready, uint64_t timeout_milliseconds);

        template <class Change>
        void unpark(Change change);

    private:
        std::mutex              _mutex;
        std::condition_variable _condition;
    };

    template <class Ready>
    bool Parker::park(Ready ready, uint64_t timeout_milliseconds)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (timeout_milliseconds == 0)
        {
            _condition.wait(lock, ready);
            return true;
        }
        uint64_t remaining = timeout_milliseconds;
        while (remaining > 0)
        {
            using Count = std::chrono::milliseconds::rep;
            const bool is_short = remaining < MAX_SLICE_MILLISECONDS;
            const uint64_t slice = is_short ? remaining : MAX_SLICE_MILLISECONDS;
            const std::chrono::milliseconds wait(static_cast<Count>(slice));
            const bool satisfied = _condition.wait_for(lock, wait, ready);
            if (satisfied) { return true; }
            remaining -= slice;
        }
        return ready();
    }

    template <class Change>
    void Parker::unpark(Change change)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            change();
        }
        _condition.notify_all();
    }
}
