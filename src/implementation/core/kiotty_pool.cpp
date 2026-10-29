#include "core/kiotty_pool.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace kiotty
{
    namespace
    {
        const size_t CLASS_BLOCK_SIZES[Pool::CLASS_COUNT] = { 64, 128, 256, 512, 1024, 4096, 16384, 65536 };

        const uint64_t FALLBACK_MAGIC = 0x4B4E4554464C4250ull;

        struct FallbackHeader
        {
            uint64_t magic;
            uint64_t size;
        };

        static_assert(sizeof(FallbackHeader) == Pool::ALIGNMENT, "FallbackHeader must keep blocks aligned to Pool::ALIGNMENT");

        FallbackHeader* fallbackHeaderOf(void* block)
        {
            char* base = static_cast<char*>(block) - sizeof(FallbackHeader);
            FallbackHeader* header = reinterpret_cast<FallbackHeader*>(base);
            if (header->magic != FALLBACK_MAGIC)
            {
                return nullptr;
            }
            return header;
        }

        void*& nextFreeAfter(void* block)
        {
            return *static_cast<void**>(block);
        }
    }

    Pool::Region::Region()
        : _memory(nullptr)
        , _storage(nullptr)
        , _block_size(0)
        , _block_count(0)
        , _free_head(nullptr)
    {
    }

    Pool::Region::~Region()
    {
        std::free(_memory);
    }

    void Pool::Region::reserve(size_t block_size, size_t block_count)
    {
        if (block_count == 0)
        {
            return;
        }
        size_t bytes = block_size * block_count;
        _memory = std::malloc(bytes + ALIGNMENT - 1);
        if (_memory == nullptr)
        {
            return;
        }
        uintptr_t address = reinterpret_cast<uintptr_t>(_memory);
        uintptr_t aligned = (address + ALIGNMENT - 1) & ~static_cast<uintptr_t>(ALIGNMENT - 1);
        _storage = reinterpret_cast<char*>(aligned);
        _block_size = block_size;
        _block_count = block_count;
        std::memset(_storage, 0, bytes);
        for (size_t i = block_count; i > 0; --i)
        {
            release(_storage + (i - 1) * block_size);
        }
    }

    size_t Pool::Region::blockSize() const
    {
        return _block_size;
    }

    bool Pool::Region::full() const
    {
        return _free_head == nullptr;
    }

    bool Pool::Region::holds(const void* block) const
    {
        if (_storage == nullptr)
        {
            return false;
        }
        uintptr_t address = reinterpret_cast<uintptr_t>(block);
        uintptr_t start = reinterpret_cast<uintptr_t>(_storage);
        uintptr_t end = start + _block_size * _block_count;
        return address >= start && address < end;
    }

    void* Pool::Region::allocate()
    {
        void* block = _free_head;
        if (block == nullptr)
        {
            return nullptr;
        }
        _free_head = nextFreeAfter(block);
        return block;
    }

    void Pool::Region::release(void* block)
    {
        nextFreeAfter(block) = _free_head;
        _free_head = block;
    }

    Pool::Pool(size_t bytes)
        : _fallback_count(0)
    {
        size_t share = bytes / CLASS_COUNT;
        for (size_t i = 0; i < CLASS_COUNT; ++i)
        {
            size_t block_size = CLASS_BLOCK_SIZES[i];
            size_t block_count = share / block_size;
            _regions[i].reserve(block_size, block_count);
        }
    }

    void* Pool::allocate(size_t size)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return allocateLocked(size);
    }

    void* Pool::reallocate(void* block, size_t size)
    {
        if (block == nullptr)
        {
            return allocate(size);
        }
        std::lock_guard<std::mutex> lock(_mutex);
        Region* region = regionOf(block);
        size_t old_size = 0;
        if (region != nullptr)
        {
            old_size = region->blockSize();
        }
        else
        {
            FallbackHeader* header = fallbackHeaderOf(block);
            assert(header != nullptr);
            if (header == nullptr)
            {
                return nullptr;
            }
            old_size = static_cast<size_t>(header->size);
        }
        if (region != nullptr && size <= old_size)
        {
            return block;
        }
        void* moved = allocateLocked(size);
        if (moved == nullptr)
        {
            return nullptr;
        }
        size_t kept = old_size < size ? old_size : size;
        std::memcpy(moved, block, kept);
        releaseLocked(block);
        return moved;
    }

    void Pool::release(void* block)
    {
        if (block == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(_mutex);
        releaseLocked(block);
    }

    size_t Pool::fallbackCount() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _fallback_count;
    }

    Pool::Region* Pool::regionFor(size_t size)
    {
        for (size_t i = 0; i < CLASS_COUNT; ++i)
        {
            if (CLASS_BLOCK_SIZES[i] >= size)
            {
                return &_regions[i];
            }
        }
        return nullptr;
    }

    Pool::Region* Pool::regionOf(const void* block)
    {
        for (size_t i = 0; i < CLASS_COUNT; ++i)
        {
            if (_regions[i].holds(block))
            {
                return &_regions[i];
            }
        }
        return nullptr;
    }

    void* Pool::allocateLocked(size_t size)
    {
        if (size == 0)
        {
            size = 1;
        }
        Region* region = regionFor(size);
        if (region != nullptr && !region->full())
        {
            return region->allocate();
        }
        return allocateFallback(size);
    }

    void* Pool::allocateFallback(size_t size)
    {
        if (size > SIZE_MAX - sizeof(FallbackHeader))
        {
            return nullptr;
        }
        void* memory = std::malloc(sizeof(FallbackHeader) + size);
        if (memory == nullptr)
        {
            return nullptr;
        }
        ++_fallback_count;
        FallbackHeader* header = static_cast<FallbackHeader*>(memory);
        header->magic = FALLBACK_MAGIC;
        header->size = size;
        char* base = static_cast<char*>(memory);
        return base + sizeof(FallbackHeader);
    }

    void Pool::releaseLocked(void* block)
    {
        Region* region = regionOf(block);
        if (region != nullptr)
        {
            region->release(block);
            return;
        }
        FallbackHeader* header = fallbackHeaderOf(block);
        assert(header != nullptr);
        if (header == nullptr)
        {
            return;
        }
        header->magic = 0;
        std::free(header);
    }
}
