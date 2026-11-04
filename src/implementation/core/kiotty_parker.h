#if !defined(KIOTTY_PARKER_H)
#define KIOTTY_PARKER_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace kiotty
{
    class Parker
    {
    public:
        static const uint64_t MAX_SLICE_MS = 86400000;

        Parker() = default;
        Parker(const Parker&) = delete;
        Parker& operator=(const Parker&) = delete;

        template <class Ready>
        bool park(Ready ready, uint64_t timeout_ms);

        template <class Change>
        void unpark(Change change);

    private:
        std::mutex              _mutex;
        std::condition_variable _cv;
    };

    template <class Ready>
    bool Parker::park(Ready ready, uint64_t timeout_ms)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (timeout_ms == 0)
        {
            _cv.wait(lock, ready);
            return true;
        }
        uint64_t remaining = timeout_ms;
        while (remaining > 0)
        {
            uint64_t slice = remaining < MAX_SLICE_MS ? remaining : MAX_SLICE_MS;
            std::chrono::milliseconds wait(static_cast<std::chrono::milliseconds::rep>(slice));
            if (_cv.wait_for(lock, wait, ready))
            {
                return true;
            }
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
        _cv.notify_all();
    }
}

#endif
