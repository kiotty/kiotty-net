#if !defined(KIOTTY_POOL_H)
#define KIOTTY_POOL_H

#include "core/kiotty_pool_layout.h"

#include <cstddef>
#include <mutex>

namespace kiotty
{
    class Pool
    {
    public:
        static const size_t MAX_CLASSES = 16;

        Pool(const PoolClass* classes, size_t class_count);
        ~Pool();

        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

        bool   valid() const;
        void*  allocate(size_t bytes);
        void*  reallocate(void* block, size_t bytes);
        bool   release(void* block);
        size_t failureCount() const;

    private:
        struct Region
        {
            char*  storage;
            size_t block_bytes;
            size_t block_count;
            size_t untouched;
            void*  free_head;
        };

        static bool available(const Region& region);

        Region* fitting(size_t bytes);
        Region* regionOf(const void* block);
        void*   take(Region& region);
        void*   allocateLocked(size_t bytes);
        void    releaseLocked(Region& region, void* block);

        void*              _memory;
        Region             _regions[MAX_CLASSES];
        size_t             _region_count;
        size_t             _failures;
        mutable std::mutex _mutex;
    };
}

#endif
