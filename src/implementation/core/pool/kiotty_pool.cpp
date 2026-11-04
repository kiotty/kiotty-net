#include "core/pool/kiotty_pool.h"

#include <cstring>
#include <functional>

namespace kiotty
{
    namespace
    {
        bool within(const unsigned char* address, const unsigned char* begin, size_t length)
        {
            std::less<const unsigned char*> before;
            if (before(address, begin))
            {
                return false;
            }
            const unsigned char* end = begin + length;
            return before(address, end);
        }
    }

    ArenaPool::ArenaPool(unsigned char* arena, const PoolClass* classes, size_t class_count)
        : _regions()
        , _region_count(0)
        , _failures(0)
    {
        if (class_count > POOL_MAX_CLASSES)
        {
            return;
        }
        unsigned char* cursor = arena;
        for (size_t i = 0; i < class_count; ++i)
        {
            Region& region = _regions[i];
            region.storage = cursor;
            region.block_bytes = classes[i].block_bytes;
            region.block_count = classes[i].block_count;
            region.untouched = 0;
            region.free_next = 0;
            cursor += region.block_bytes * region.block_count;
        }
        _region_count = class_count;
    }

    void* ArenaPool::allocate(size_t bytes)
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return allocateLocked(bytes);
    }

    void* ArenaPool::reallocate(void* block, size_t bytes)
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

    bool ArenaPool::release(void* block)
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

    bool ArenaPool::holds(const void* block) const
    {
        return regionIndexOf(block) != _region_count;
    }

    size_t ArenaPool::failureCount() const
    {
        std::lock_guard<std::mutex> guard(_mutex);
        return _failures;
    }

    bool ArenaPool::available(const Region& region)
    {
        return region.free_next != 0 || region.untouched < region.block_count;
    }

    ArenaPool::Region* ArenaPool::fitting(size_t bytes)
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

    size_t ArenaPool::regionIndexOf(const void* block) const
    {
        const unsigned char* address = static_cast<const unsigned char*>(block);
        for (size_t i = 0; i < _region_count; ++i)
        {
            const Region& region = _regions[i];
            size_t length = region.block_bytes * region.block_count;
            if (!within(address, region.storage, length))
            {
                continue;
            }
            size_t offset = static_cast<size_t>(address - region.storage);
            if (offset % region.block_bytes != 0)
            {
                return _region_count;
            }
            return i;
        }
        return _region_count;
    }

    ArenaPool::Region* ArenaPool::regionOf(const void* block)
    {
        size_t index = regionIndexOf(block);
        if (index == _region_count)
        {
            return nullptr;
        }
        return &_regions[index];
    }

    void* ArenaPool::take(Region& region)
    {
        size_t index = region.untouched;
        if (region.free_next != 0)
        {
            index = region.free_next - 1;
            std::memcpy(&region.free_next, region.storage + index * region.block_bytes, sizeof(region.free_next));
        }
        else
        {
            region.untouched += 1;
        }
        return region.storage + index * region.block_bytes;
    }

    void* ArenaPool::allocateLocked(size_t bytes)
    {
        Region* region = fitting(bytes);
        if (region == nullptr)
        {
            _failures += 1;
            return nullptr;
        }
        return take(*region);
    }

    void ArenaPool::releaseLocked(Region& region, void* block)
    {
        unsigned char* address = static_cast<unsigned char*>(block);
        size_t index = static_cast<size_t>(address - region.storage) / region.block_bytes;
        std::memcpy(address, &region.free_next, sizeof(region.free_next));
        region.free_next = index + 1;
    }
}
