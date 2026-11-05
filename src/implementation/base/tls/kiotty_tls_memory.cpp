#include "base/tls/kiotty_tls_memory.h"

#include <cassert>
#include <functional>

namespace kiotty
{
    namespace net
    {
        TlsMemory::GlobalArena         TlsMemory::s_global;
        TlsMemory::Range               TlsMemory::s_ranges[Limits::OWNERS];
        std::atomic_flag               TlsMemory::s_ranges_lock = ATOMIC_FLAG_INIT;
        std::atomic_flag               TlsMemory::s_install_lock = ATOMIC_FLAG_INIT;
        std::atomic<bool>              TlsMemory::s_install_tried(false);
        std::atomic<bool>              TlsMemory::s_installed(false);
        std::atomic<size_t>            TlsMemory::s_late_frees(0);

        void* TlsMemory::GlobalArena::allocate(size_t bytes)
        {
            return GlobalTlsPool::allocate(bytes);
        }

        void* TlsMemory::GlobalArena::reallocate(void* block, size_t bytes)
        {
            return GlobalTlsPool::reallocate(block, bytes);
        }

        bool TlsMemory::GlobalArena::release(void* block)
        {
            return GlobalTlsPool::release(block);
        }

        bool TlsMemory::GlobalArena::holds(const void* block) const
        {
            return GlobalTlsPool::holds(block);
        }

        size_t TlsMemory::GlobalArena::failureCount() const
        {
            size_t total = 0;
            for (size_t i = 0; i < GlobalTlsPool::CLASS_COUNT; ++i)
            {
                total += GlobalTlsPool::usage(i).failures;
            }
            return total;
        }

        size_t TlsMemory::GlobalArena::liveCount() const
        {
            size_t total = 0;
            for (size_t i = 0; i < GlobalTlsPool::CLASS_COUNT; ++i)
            {
                total += GlobalTlsPool::usage(i).live;
            }
            return total;
        }

        TlsMemoryFunctions TlsMemory::functions()
        {
            TlsMemoryFunctions result;
            result.allocate = &TlsMemory::allocate;
            result.reallocate = &TlsMemory::reallocate;
            result.release = &TlsMemory::release;
            return result;
        }

        bool TlsMemory::installGlobal()
        {
            while (s_install_lock.test_and_set(std::memory_order_acquire))
            {
            }
            if (!s_install_tried.load())
            {
                s_install_tried.store(true);
                bool hooked = installTlsMemory(functions());
                s_installed.store(hooked);
                if (hooked)
                {
                    Thread& state = thread();
                    PoolBase* previous = state.current;
                    state.current = &s_global;
                    attachThread();
                    warmTlsMemory();
                    state.current = previous;
                }
            }
            bool installed = s_installed.load();
            s_install_lock.clear(std::memory_order_release);
            return installed;
        }

        bool TlsMemory::installed()
        {
            return s_installed.load();
        }

        void TlsMemory::detachThread()
        {
            Thread& state = thread();
            state.current = &s_global;
            state.last_hit = nullptr;
            state.attached = false;
            if (!installed())
            {
                return;
            }
            detachTlsThread();
        }

        size_t TlsMemory::lateFrees()
        {
            return s_late_frees.load();
        }

        size_t TlsMemory::globalClassCount()
        {
            return GlobalTlsPool::CLASS_COUNT;
        }

        GlobalPoolUsage TlsMemory::globalUsage(size_t index)
        {
            return GlobalTlsPool::usage(index);
        }

        size_t TlsMemory::globalLiveBlocks()
        {
            return s_global.liveCount();
        }

        TlsMemory::Scope::Scope(TlsMemory& owner)
            : _previous(*thread().current)
        {
            attachThread();
            if (owner.published())
            {
                thread().current = &owner._pool;
            }
        }

        TlsMemory::Scope::~Scope()
        {
            thread().current = &_previous;
        }

        TlsMemory::TlsMemory(PoolBase& pool, const unsigned char* arena, size_t arena_bytes)
            : _pool(pool)
            , _range(nullptr)
        {
            if (arena_bytes == 0)
            {
                return;
            }
            lockRanges();
            for (Range& range : s_ranges)
            {
                if (range.pool.load(std::memory_order_relaxed) != nullptr)
                {
                    continue;
                }
                range.begin.store(arena, std::memory_order_relaxed);
                range.end.store(arena + arena_bytes, std::memory_order_relaxed);
                range.pool.store(&_pool, std::memory_order_release);
                _range = &range;
                break;
            }
            unlockRanges();
            assert(_range != nullptr);
        }

        TlsMemory::~TlsMemory()
        {
            if (_range == nullptr)
            {
                return;
            }
            lockRanges();
            _range->pool.store(nullptr, std::memory_order_release);
            _range->begin.store(nullptr, std::memory_order_relaxed);
            _range->end.store(nullptr, std::memory_order_relaxed);
            unlockRanges();
        }

        bool TlsMemory::published() const
        {
            return _range != nullptr;
        }

        size_t TlsMemory::failures() const
        {
            return _pool.failureCount();
        }

        size_t TlsMemory::liveBlocks() const
        {
            return _pool.liveCount();
        }

        TlsMemory::Thread& TlsMemory::thread()
        {
            static thread_local Thread state = { &s_global, nullptr, false };
            return state;
        }

        void TlsMemory::attachThread()
        {
            Thread& state = thread();
            if (state.attached || !installed())
            {
                return;
            }
            state.attached = true;
            PoolBase* previous = state.current;
            state.current = &s_global;
            attachTlsThread();
            state.current = previous;
        }

        void* TlsMemory::allocate(size_t bytes, const char* file, int line)
        {
            (void)line;
            if (bytes == 0)
            {
                return nullptr;
            }
            PoolBase* pool = thread().current;
            if (tlsThreadStateFile(file))
            {
                pool = &s_global;
            }
            return pool->allocate(bytes);
        }

        void* TlsMemory::reallocate(void* block, size_t bytes, const char* file, int line)
        {
            if (block == nullptr)
            {
                return allocate(bytes, file, line);
            }
            PoolBase* arena = arenaOf(block);
            if (arena == nullptr)
            {
                s_late_frees.fetch_add(1);
                return nullptr;
            }
            return arena->reallocate(block, bytes);
        }

        void TlsMemory::release(void* block, const char* file, int line)
        {
            (void)file;
            (void)line;
            if (block == nullptr)
            {
                return;
            }
            PoolBase* arena = arenaOf(block);
            bool released = arena != nullptr && arena->release(block);
            if (!released)
            {
                s_late_frees.fetch_add(1);
            }
        }

        PoolBase* TlsMemory::arenaOf(const void* block)
        {
            Thread& state = thread();
            PoolBase* pool = nullptr;
            if (state.last_hit != nullptr && within(*state.last_hit, block, pool))
            {
                return pool;
            }
            for (const Range& range : s_ranges)
            {
                if (within(range, block, pool))
                {
                    state.last_hit = &range;
                    return pool;
                }
            }
            if (s_global.holds(block))
            {
                return &s_global;
            }
            return nullptr;
        }

        bool TlsMemory::within(const Range& range, const void* block, PoolBase*& pool)
        {
            PoolBase* owner = range.pool.load(std::memory_order_acquire);
            if (owner == nullptr)
            {
                return false;
            }
            const unsigned char* address = static_cast<const unsigned char*>(block);
            const unsigned char* begin = range.begin.load(std::memory_order_relaxed);
            const unsigned char* end = range.end.load(std::memory_order_relaxed);
            std::less<const unsigned char*> before;
            if (before(address, begin) || !before(address, end))
            {
                return false;
            }
            pool = owner;
            return true;
        }

        void TlsMemory::lockRanges()
        {
            while (s_ranges_lock.test_and_set(std::memory_order_acquire))
            {
            }
        }

        void TlsMemory::unlockRanges()
        {
            s_ranges_lock.clear(std::memory_order_release);
        }
    }
}
