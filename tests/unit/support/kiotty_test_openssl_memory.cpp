#include "support/kiotty_test_openssl_memory.h"

#if KIOTTY_TEST_OPENSSL

#include <openssl/crypto.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace kiotty_test
{
    namespace
    {
        union BlockHeader
        {
            size_t           size;
            std::max_align_t alignment;
        };

        std::atomic<size_t> g_live_bytes(0);
        std::atomic<size_t> g_live_blocks(0);
        std::atomic<size_t> g_peak_bytes(0);

        void raisePeak(size_t bytes)
        {
            size_t peak = g_peak_bytes.load();
            while (bytes > peak && !g_peak_bytes.compare_exchange_weak(peak, bytes))
            {
            }
        }

        void* countedAllocate(size_t size)
        {
            if (size > SIZE_MAX - sizeof(BlockHeader))
            {
                return nullptr;
            }
            BlockHeader* header = static_cast<BlockHeader*>(std::malloc(sizeof(BlockHeader) + size));
            if (header == nullptr)
            {
                return nullptr;
            }
            header->size = size;
            size_t live = g_live_bytes.fetch_add(size) + size;
            g_live_blocks.fetch_add(1);
            raisePeak(live);
            return header + 1;
        }

        void countedRelease(void* block)
        {
            if (block == nullptr)
            {
                return;
            }
            BlockHeader* header = static_cast<BlockHeader*>(block) - 1;
            g_live_bytes.fetch_sub(header->size);
            g_live_blocks.fetch_sub(1);
            std::free(header);
        }

        void* hookAllocate(size_t size, const char* file, int line)
        {
            (void)file;
            (void)line;
            return countedAllocate(size);
        }

        void* hookReallocate(void* block, size_t size, const char* file, int line)
        {
            (void)file;
            (void)line;
            if (block == nullptr)
            {
                return countedAllocate(size);
            }
            if (size == 0)
            {
                countedRelease(block);
                return nullptr;
            }
            void* moved = countedAllocate(size);
            if (moved == nullptr)
            {
                return nullptr;
            }
            BlockHeader* header = static_cast<BlockHeader*>(block) - 1;
            size_t kept = header->size < size ? header->size : size;
            std::memcpy(moved, block, kept);
            countedRelease(block);
            return moved;
        }

        void hookRelease(void* block, const char* file, int line)
        {
            (void)file;
            (void)line;
            countedRelease(block);
        }

        const bool g_counting = CRYPTO_set_mem_functions(hookAllocate, hookReallocate, hookRelease) == 1;
    }

    OpenSslMemory openSslMemory()
    {
        OpenSslMemory memory;
        memory.counting = g_counting;
        memory.live_bytes = g_live_bytes.load();
        memory.live_blocks = g_live_blocks.load();
        memory.peak_bytes = g_peak_bytes.load();
        return memory;
    }

    void resetOpenSslPeak()
    {
        g_peak_bytes.store(g_live_bytes.load());
    }
}

#else

namespace kiotty_test
{
    OpenSslMemory openSslMemory()
    {
        OpenSslMemory memory;
        memory.counting = false;
        memory.live_bytes = 0;
        memory.live_blocks = 0;
        memory.peak_bytes = 0;
        return memory;
    }

    void resetOpenSslPeak()
    {
    }
}

#endif
