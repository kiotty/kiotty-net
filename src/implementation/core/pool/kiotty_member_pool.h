#if !defined(KIOTTY_MEMBER_POOL_H)
#define KIOTTY_MEMBER_POOL_H

#include "core/pool/kiotty_pool_layout.h"

#include <cstddef>
#include <mutex>

namespace kiotty
{
    class PoolBase
    {
    public:
        PoolBase() = default;
        PoolBase(const PoolBase&) = delete;
        PoolBase& operator=(const PoolBase&) = delete;

        virtual void*  allocate(size_t bytes) = 0;
        virtual void*  reallocate(void* block, size_t bytes) = 0;
        virtual bool   release(void* block) = 0;
        virtual bool   holds(const void* block) const = 0;
        virtual size_t failureCount() const = 0;
        virtual size_t liveCount() const = 0;

    protected:
        ~PoolBase() = default;
    };

    class ArenaPool : public PoolBase
    {
    public:
        void*  allocate(size_t bytes) override;
        void*  reallocate(void* block, size_t bytes) override;
        bool   release(void* block) override;
        bool   holds(const void* block) const override;
        size_t failureCount() const override;
        size_t liveCount() const override;

    protected:
        ArenaPool(unsigned char* arena, const PoolClass* classes, size_t class_count);
        ~ArenaPool() = default;

    private:
        struct Region
        {
            unsigned char* storage;
            size_t         block_bytes;
            size_t         block_count;
            size_t         untouched;
            size_t         free_next;
        };

        static bool available(const Region& region);

        Region*       fitting(size_t bytes);
        size_t        regionIndexOf(const void* block) const;
        Region*       regionOf(const void* block);
        void*         take(Region& region);
        void*         allocateLocked(size_t bytes);
        void          releaseLocked(Region& region, void* block);

        Region             _regions[POOL_MAX_CLASSES];
        size_t             _region_count;
        size_t             _failures;
        size_t             _live;
        mutable std::mutex _mutex;
    };

    template <size_t Bytes>
    struct PoolArena
    {
        alignas(POOL_ALIGNMENT) unsigned char bytes[Bytes > 0 ? Bytes : POOL_ALIGNMENT];
    };

    template <class Layout>
    class MemberPool : private PoolArena<Layout::ARENA_BYTES>, public ArenaPool
    {
    public:
        static const size_t ARENA_BYTES = Layout::ARENA_BYTES;

        MemberPool()
            : ArenaPool(this->bytes, Layout::classes(), Layout::CLASS_COUNT)
        {
        }

        const unsigned char* arena() const
        {
            return this->bytes;
        }
    };

    template <class Layout>
    const size_t MemberPool<Layout>::ARENA_BYTES;
}

#endif
