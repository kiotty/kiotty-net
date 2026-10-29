#if !defined(KIOTTY_POOL_H)
#define KIOTTY_POOL_H

#include <cstddef>
#include <mutex>

namespace kiotty
{
    class Pool
    {
    public:
        static const size_t CLASS_COUNT = 8;
        static const size_t ALIGNMENT = 16;

        explicit Pool(size_t bytes);

        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

        void*  allocate(size_t size);
        void*  reallocate(void* block, size_t size);
        void   release(void* block);
        size_t fallbackCount() const;

    private:
        class Region
        {
        public:
            Region();
            ~Region();

            Region(const Region&) = delete;
            Region& operator=(const Region&) = delete;

            void   reserve(size_t block_size, size_t block_count);
            size_t blockSize() const;
            bool   full() const;
            bool   holds(const void* block) const;
            void*  allocate();
            void   release(void* block);

        private:
            void*  _memory;
            char*  _storage;
            size_t _block_size;
            size_t _block_count;
            void*  _free_head;
        };

        Region* regionFor(size_t size);
        Region* regionOf(const void* block);
        void*   allocateLocked(size_t size);
        void*   allocateFallback(size_t size);
        void    releaseLocked(void* block);

        Region             _regions[CLASS_COUNT];
        size_t             _fallback_count;
        mutable std::mutex _mutex;
    };
}

#endif
