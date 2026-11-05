#if !defined(KIOTTY_GLOBAL_POOL_H)
#define KIOTTY_GLOBAL_POOL_H

#include "core/pool/kiotty_pool_layout.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

namespace kiotty
{
    struct GlobalPoolUsage
    {
        size_t block_bytes;
        size_t block_count;
        size_t live;
        size_t peak;
        size_t failures;
    };

    template <class Tag, size_t Index, class Class>
    class GlobalClassStorage
    {
    public:
        static const size_t ARENA_BYTES = Class::BLOCK_BYTES * Class::BLOCK_COUNT;

        GlobalClassStorage() = delete;

        static void* take()
        {
            lock();
            void* block = takeLocked();
            unlock();
            return block;
        }

        static void put(void* block)
        {
            size_t index = indexOf(block);
            lock();
            std::memcpy(block, &s_free_next, sizeof(s_free_next));
            s_free_next = index + 1;
            s_live -= 1;
            unlock();
        }

        static bool holds(const void* block)
        {
            const unsigned char* address = static_cast<const unsigned char*>(block);
            std::less<const unsigned char*> before;
            if (before(address, s_arena))
            {
                return false;
            }
            return before(address, s_arena + ARENA_BYTES);
        }

        static bool aligned(const void* block)
        {
            return offsetOf(block) % Class::BLOCK_BYTES == 0;
        }

        static void fail()
        {
            lock();
            s_failures += 1;
            unlock();
        }

        static GlobalPoolUsage usage()
        {
            GlobalPoolUsage result;
            result.block_bytes = Class::BLOCK_BYTES;
            result.block_count = Class::BLOCK_COUNT;
            lock();
            result.live = s_live;
            result.peak = s_peak;
            result.failures = s_failures;
            unlock();
            return result;
        }

    private:
        static void lock()
        {
            while (s_lock.test_and_set(std::memory_order_acquire))
            {
            }
        }

        static void unlock()
        {
            s_lock.clear(std::memory_order_release);
        }

        static size_t offsetOf(const void* block)
        {
            const unsigned char* address = static_cast<const unsigned char*>(block);
            return static_cast<size_t>(address - s_arena);
        }

        static size_t indexOf(const void* block)
        {
            return offsetOf(block) / Class::BLOCK_BYTES;
        }

        static unsigned char* blockAt(size_t index)
        {
            return s_arena + index * Class::BLOCK_BYTES;
        }

        static void* takeLocked()
        {
            unsigned char* block = nullptr;
            if (s_free_next != 0)
            {
                block = blockAt(s_free_next - 1);
                std::memcpy(&s_free_next, block, sizeof(s_free_next));
            }
            else if (s_untouched < Class::BLOCK_COUNT)
            {
                block = blockAt(s_untouched);
                s_untouched += 1;
            }
            if (block == nullptr)
            {
                return nullptr;
            }
            s_live += 1;
            if (s_live > s_peak)
            {
                s_peak = s_live;
            }
            return block;
        }

        alignas(POOL_ALIGNMENT) static unsigned char s_arena[ARENA_BYTES > 0 ? ARENA_BYTES : POOL_ALIGNMENT];
        static std::atomic_flag s_lock;
        static size_t           s_free_next;
        static size_t           s_untouched;
        static size_t           s_live;
        static size_t           s_peak;
        static size_t           s_failures;
    };

    template <class Tag, size_t Index, class Class>
    alignas(POOL_ALIGNMENT) unsigned char GlobalClassStorage<Tag, Index, Class>::s_arena[ARENA_BYTES > 0 ? ARENA_BYTES : POOL_ALIGNMENT];

    template <class Tag, size_t Index, class Class>
    std::atomic_flag GlobalClassStorage<Tag, Index, Class>::s_lock = ATOMIC_FLAG_INIT;

    template <class Tag, size_t Index, class Class>
    size_t GlobalClassStorage<Tag, Index, Class>::s_free_next = 0;

    template <class Tag, size_t Index, class Class>
    size_t GlobalClassStorage<Tag, Index, Class>::s_untouched = 0;

    template <class Tag, size_t Index, class Class>
    size_t GlobalClassStorage<Tag, Index, Class>::s_live = 0;

    template <class Tag, size_t Index, class Class>
    size_t GlobalClassStorage<Tag, Index, Class>::s_peak = 0;

    template <class Tag, size_t Index, class Class>
    size_t GlobalClassStorage<Tag, Index, Class>::s_failures = 0;

    template <class Tag, size_t Index, class... Classes>
    class GlobalPoolLevel;

    template <class Tag, size_t Index>
    class GlobalPoolLevel<Tag, Index>
    {
    public:
        static const size_t ARENA_BYTES = 0;

        static size_t fitting(size_t bytes, size_t above)
        {
            (void)bytes;
            (void)above;
            return 0;
        }

        static void* take(size_t block_bytes)
        {
            (void)block_bytes;
            return nullptr;
        }

        static void fail(size_t block_bytes)
        {
            (void)block_bytes;
        }

        static size_t blockBytes(const void* block)
        {
            (void)block;
            return 0;
        }

        static bool release(void* block)
        {
            (void)block;
            return false;
        }

        static GlobalPoolUsage usage(size_t index)
        {
            (void)index;
            GlobalPoolUsage result = { 0, 0, 0, 0, 0 };
            return result;
        }
    };

    template <class Tag, size_t Index, class Class, class... Rest>
    class GlobalPoolLevel<Tag, Index, Class, Rest...>
    {
    public:
        typedef GlobalClassStorage<Tag, Index, Class> Storage;
        typedef GlobalPoolLevel<Tag, Index + 1, Rest...> Next;

        static const size_t ARENA_BYTES = Storage::ARENA_BYTES + Next::ARENA_BYTES;

        static size_t fitting(size_t bytes, size_t above)
        {
            size_t rest = Next::fitting(bytes, above);
            bool fits = Class::BLOCK_COUNT > 0 && Class::BLOCK_BYTES >= bytes && Class::BLOCK_BYTES > above;
            if (!fits)
            {
                return rest;
            }
            if (rest != 0 && rest < Class::BLOCK_BYTES)
            {
                return rest;
            }
            return Class::BLOCK_BYTES;
        }

        static void* take(size_t block_bytes)
        {
            if (Class::BLOCK_BYTES == block_bytes)
            {
                void* block = Storage::take();
                if (block != nullptr)
                {
                    return block;
                }
            }
            return Next::take(block_bytes);
        }

        static void fail(size_t block_bytes)
        {
            if (Class::BLOCK_BYTES == block_bytes)
            {
                Storage::fail();
                return;
            }
            Next::fail(block_bytes);
        }

        static size_t blockBytes(const void* block)
        {
            if (Class::BLOCK_COUNT > 0 && Storage::holds(block))
            {
                return Class::BLOCK_BYTES;
            }
            return Next::blockBytes(block);
        }

        static bool release(void* block)
        {
            if (Class::BLOCK_COUNT == 0 || !Storage::holds(block))
            {
                return Next::release(block);
            }
            if (!Storage::aligned(block))
            {
                return false;
            }
            Storage::put(block);
            return true;
        }

        static GlobalPoolUsage usage(size_t index)
        {
            if (index == 0)
            {
                return Storage::usage();
            }
            return Next::usage(index - 1);
        }
    };

    template <class Tag, class Layout>
    class GlobalPool;

    template <class Tag, class... Classes>
    class GlobalPool<Tag, PoolLayout<Classes...>>
    {
    public:
        typedef GlobalPoolLevel<Tag, 0, Classes...> Levels;

        static const size_t CLASS_COUNT = sizeof...(Classes);
        static const size_t ARENA_BYTES = Levels::ARENA_BYTES;

        GlobalPool() = delete;

        static void* allocate(size_t bytes)
        {
            size_t above = 0;
            size_t first = Levels::fitting(bytes, above);
            size_t block_bytes = first;
            while (block_bytes != 0)
            {
                void* block = Levels::take(block_bytes);
                if (block != nullptr)
                {
                    return block;
                }
                above = block_bytes;
                block_bytes = Levels::fitting(bytes, above);
            }
            Levels::fail(first);
            return nullptr;
        }

        static void* reallocate(void* block, size_t bytes)
        {
            if (block == nullptr)
            {
                return allocate(bytes);
            }
            size_t held = Levels::blockBytes(block);
            if (held == 0)
            {
                return nullptr;
            }
            if (bytes == 0)
            {
                release(block);
                return nullptr;
            }
            if (bytes <= held)
            {
                return block;
            }
            void* moved = allocate(bytes);
            if (moved == nullptr)
            {
                return nullptr;
            }
            std::memcpy(moved, block, held);
            release(block);
            return moved;
        }

        static bool release(void* block)
        {
            if (block == nullptr)
            {
                return true;
            }
            return Levels::release(block);
        }

        static bool holds(const void* block)
        {
            return Levels::blockBytes(block) != 0;
        }

        static size_t blockBytes(const void* block)
        {
            return Levels::blockBytes(block);
        }

        static GlobalPoolUsage usage(size_t index)
        {
            return Levels::usage(index);
        }
    };
}

#endif
