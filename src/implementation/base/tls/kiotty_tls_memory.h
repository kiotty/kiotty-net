#if !defined(KIOTTY_TLS_MEMORY_H)
#define KIOTTY_TLS_MEMORY_H

#include "base/pool/kiotty_memory_layout.h"
#include "core/pool/kiotty_global_pool.h"
#include "core/pool/kiotty_member_pool.h"

#include <atomic>
#include <cstddef>

namespace kiotty
{
    namespace net
    {
        struct TlsMemoryFunctions
        {
            void* (*allocate)(size_t bytes, const char* file, int line);
            void* (*reallocate)(void* block, size_t bytes, const char* file, int line);
            void  (*release)(void* block, const char* file, int line);
        };

        bool installTlsMemory(const TlsMemoryFunctions& functions);
        void warmTlsMemory();
        void attachTlsThread();
        void detachTlsThread();
        bool tlsThreadStateFile(const char* file);

        class TlsMemory
        {
        public:
            static TlsMemoryFunctions functions();
            static bool               installGlobal();
            static bool               installed();
            static void               detachThread();
            static size_t             lateFrees();
            static size_t             globalClassCount();
            static GlobalPoolUsage    globalUsage(size_t index);
            static size_t             globalLiveBlocks();

            class Scope
            {
            public:
                explicit Scope(TlsMemory& owner);
                ~Scope();

                Scope(const Scope&) = delete;
                Scope& operator=(const Scope&) = delete;

            private:
                PoolBase& _previous;
            };

            TlsMemory(const TlsMemory&) = delete;
            TlsMemory& operator=(const TlsMemory&) = delete;

            bool   published() const;
            size_t failures() const;
            size_t liveBlocks() const;

        protected:
            TlsMemory(PoolBase& pool, const unsigned char* arena, size_t arena_bytes);
            ~TlsMemory();

        private:
            struct GlobalTlsTag;
            typedef GlobalPool<GlobalTlsTag, GlobalTlsLayout> GlobalTlsPool;

            class GlobalArena : public PoolBase
            {
            public:
                void*  allocate(size_t bytes) override;
                void*  reallocate(void* block, size_t bytes) override;
                bool   release(void* block) override;
                bool   holds(const void* block) const override;
                size_t failureCount() const override;
                size_t liveCount() const override;
            };

            struct Range
            {
                std::atomic<const unsigned char*> begin;
                std::atomic<const unsigned char*> end;
                std::atomic<PoolBase*>            pool;
            };

            struct Thread
            {
                PoolBase*    current;
                const Range* last_hit;
                bool         attached;
            };

            static Thread&   thread();
            static void      attachThread();
            static void*     allocate(size_t bytes, const char* file, int line);
            static void*     reallocate(void* block, size_t bytes, const char* file, int line);
            static void      release(void* block, const char* file, int line);
            static PoolBase* arenaOf(const void* block);
            static bool      within(const Range& range, const void* block, PoolBase*& pool);
            static void      lockRanges();
            static void      unlockRanges();

            PoolBase& _pool;
            Range*    _range;

            static GlobalArena         s_global;
            static Range               s_ranges[Limits::OWNERS];
            static std::atomic_flag    s_ranges_lock;
            static std::atomic_flag    s_install_lock;
            static std::atomic<bool>   s_install_tried;
            static std::atomic<bool>   s_installed;
            static std::atomic<size_t> s_late_frees;
        };

        template <class Layout>
        struct OwnerTlsPool
        {
            MemberPool<Layout> pool;
        };

        template <class Layout>
        class OwnerTlsMemory : private OwnerTlsPool<Layout>, public TlsMemory
        {
        public:
            OwnerTlsMemory()
                : OwnerTlsPool<Layout>()
                , TlsMemory(this->pool, this->pool.arena(), MemberPool<Layout>::ARENA_BYTES)
            {
            }
        };
    }
}

#endif
