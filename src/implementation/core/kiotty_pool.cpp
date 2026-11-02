#include "core/kiotty_pool.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace kiotty
{
    namespace
    {
        void*& nextFreeAfter(void* block)
        {
            return *static_cast<void**>(block);
        }

        bool arenaBytes(const PoolClass* classes, size_t class_count, size_t& total)
        {
            total = 0;
            for (size_t i = 0; i < class_count; ++i)
            {
                size_t block_bytes = alignBlock(classes[i].block_bytes);
                if (block_bytes != 0 && classes[i].block_count > (SIZE_MAX - total) / block_bytes)
                {
                    return false;
                }
                total += block_bytes * classes[i].block_count;
            }
            return total <= SIZE_MAX - POOL_ALIGNMENT;
        }
    }

    Pool::Pool(const PoolClass* classes, size_t class_count)
        : _memory(nullptr)
        , _region_count(0)
        , _failures(0)
    {
        size_t total = 0;
        if (class_count > MAX_CLASSES || !arenaBytes(classes, class_count, total))
        {
            return;
        }
        _memory = std::malloc(total + POOL_ALIGNMENT);
        if (_memory == nullptr)
        {
            return;
        }
        uintptr_t address = reinterpret_cast<uintptr_t>(_memory);
        uintptr_t aligned = (address + POOL_ALIGNMENT - 1) & ~static_cast<uintptr_t>(POOL_ALIGNMENT - 1);
        char* cursor = reinterpret_cast<char*>(aligned);
        for (size_t i = 0; i < class_count; ++i)
        {
            Region& region = _regions[i];
            region.storage = cursor;
            region.block_bytes = alignBlock(classes[i].block_bytes);
            region.block_count = classes[i].block_count;
            region.untouched = 0;
            region.free_head = nullptr;
            cursor += region.block_bytes * region.block_count;
        }
        _region_count = class_count;
    }

    Pool::~Pool()
    {
        std::free(_memory);
    }

    bool Pool::valid() const
    {
        return _memory != nullptr;
    }

    void* Pool::allocate(size_t bytes)
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return allocateLocked(bytes);
    }

    void* Pool::reallocate(void* block, size_t bytes)
    {
        if (block == nullptr)
        {
            return allocate(bytes);
        }
        std::lock_guard<std::mutex> guard(_mutex);
        Region* region = regionOf(block);
        if (region == nullptr)
        {
            return nullptr;
        }
        if (bytes == 0)
        {
            releaseLocked(*region, block);
            return nullptr;
        }
        if (bytes <= region->block_bytes)
        {
            return block;
        }
        void* moved = allocateLocked(bytes);
        if (moved == nullptr)
        {
            return nullptr;
        }
        std::memcpy(moved, block, region->block_bytes);
        releaseLocked(*region, block);
        return moved;
    }

    bool Pool::release(void* block)
    {
        if (block == nullptr)
        {
            return true;
        }
        std::lock_guard<std::mutex> guard(_mutex);
        Region* region = regionOf(block);
        if (region == nullptr)
        {
            return false;
        }
        releaseLocked(*region, block);
        return true;
    }

    size_t Pool::failureCount() const
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _failures;
    }

    bool Pool::available(const Region& region)
    {
        return region.free_head != nullptr || region.untouched < region.block_count;
    }

    Pool::Region* Pool::fitting(size_t bytes)
    {
        Region* best = nullptr;
        for (size_t i = 0; i < _region_count; ++i)
        {
            Region& region = _regions[i];
            bool fits = region.block_bytes >= bytes && available(region);
            if (fits && (best == nullptr || region.block_bytes < best->block_bytes))
            {
                best = &region;
            }
        }
        return best;
    }

    Pool::Region* Pool::regionOf(const void* block)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(block);
        for (size_t i = 0; i < _region_count; ++i)
        {
            Region& region = _regions[i];
            uintptr_t begin = reinterpret_cast<uintptr_t>(region.storage);
            uintptr_t end = begin + region.block_bytes * region.block_count;
            if (address >= begin && address < end && (address - begin) % region.block_bytes == 0)
            {
                return &region;
            }
        }
        return nullptr;
    }

    void* Pool::take(Region& region)
    {
        void* block = region.free_head;
        if (block != nullptr)
        {
            region.free_head = nextFreeAfter(block);
            return block;
        }
        block = region.storage + region.untouched * region.block_bytes;
        region.untouched += 1;
        return block;
    }

    void* Pool::allocateLocked(size_t bytes)
    {
        Region* region = fitting(bytes);
        if (region == nullptr)
        {
            _failures += 1;
            return nullptr;
        }
        return take(*region);
    }

    void Pool::releaseLocked(Region& region, void* block)
    {
        nextFreeAfter(block) = region.free_head;
        region.free_head = block;
    }
}
