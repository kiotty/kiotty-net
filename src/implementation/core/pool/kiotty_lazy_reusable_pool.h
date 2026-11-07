#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "core/kiotty_result.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace kiotty::net::implementation
{
    struct LazyReusablePoolConfiguration
    {
        size_t capacity;
    };

    template <class T>
    class LazyReusablePool
    {
        static_assert(is_reusable<T>::value,
                      "LazyReusablePool needs a default constructible, non copyable, "
                      "non movable T with initialize and noexcept reset");

    public:
        using code_type = typename T::code_type;
        using configuration_type = typename T::configuration_type;
        using AcquireResult = Result<T&, code_type>;

        LazyReusablePool();
        ~LazyReusablePool();

        LazyReusablePool(const LazyReusablePool&) = delete;
        LazyReusablePool& operator=(const LazyReusablePool&) = delete;

        code_type     initialize(LazyReusablePoolConfiguration configuration);
        AcquireResult acquire(configuration_type configuration);
        void          release(T& object);
        void          clear();
        size_t        capacity() const;
        size_t        count() const;

    private:
        size_t findSlot(SlotState state) const;
        size_t indexOf(const T& object) const;

        std::vector<T>         _objects;
        std::vector<SlotState> _states;
        std::vector<int32_t>   _generations;
        size_t                 _count;
    };

    template <class T>
    LazyReusablePool<T>::LazyReusablePool()
        : _objects()
        , _states()
        , _generations()
        , _count(0)
    {
    }

    template <class T>
    LazyReusablePool<T>::~LazyReusablePool()
    {
        clear();
    }

    template <class T>
    typename LazyReusablePool<T>::code_type
    LazyReusablePool<T>::initialize(LazyReusablePoolConfiguration configuration)
    {
        const code_type invalid = static_cast<code_type>(T::INVALID_ARGUMENT);
        if (!_objects.empty()) { return invalid; }
        const size_t slots = configuration.capacity;
        if (slots == 0 || slots > REUSABLE_POOL_MAX_SLOTS) { return invalid; }
        _objects = std::vector<T>(slots);
        _states.assign(slots, SlotState::EMPTY);
        _generations.assign(slots, 0);
        return code_type{};
    }

    template <class T>
    typename LazyReusablePool<T>::AcquireResult
    LazyReusablePool<T>::acquire(configuration_type configuration)
    {
        const size_t none = _objects.size();
        size_t index = findSlot(SlotState::RETIRED);
        if (index == none) { index = findSlot(SlotState::EMPTY); }
        if (index == none) { return AcquireResult::failure(static_cast<code_type>(T::TABLE_FULL)); }
        _generations[index] = detail::nextGeneration(_generations[index]);
        const PoolHandle id{ _generations[index], static_cast<int32_t>(index) };
        T& object = _objects[index];
        const code_type code = object.initialize(id, std::move(configuration));
        if (code != code_type{})
        {
            object.reset();
            _states[index] = SlotState::RETIRED;
            return AcquireResult::failure(code);
        }
        _states[index] = SlotState::ACTIVE;
        _count += 1;
        return AcquireResult::success(object);
    }

    template <class T>
    void LazyReusablePool<T>::release(T& object)
    {
        const size_t index = indexOf(object);
        const bool is_active = index < _objects.size() && _states[index] == SlotState::ACTIVE;
        assert(is_active);
        if (!is_active) { return; }
        object.reset();
        _states[index] = SlotState::RETIRED;
        _count -= 1;
    }

    template <class T>
    void LazyReusablePool<T>::clear()
    {
        for (size_t i = 0; i < _objects.size(); ++i)
        {
            if (_states[i] == SlotState::ACTIVE) { _objects[i].reset(); }
            _states[i] = SlotState::EMPTY;
        }
        _count = 0;
    }

    template <class T>
    size_t LazyReusablePool<T>::capacity() const
    {
        return _objects.size();
    }

    template <class T>
    size_t LazyReusablePool<T>::count() const
    {
        return _count;
    }

    template <class T>
    size_t LazyReusablePool<T>::findSlot(SlotState state) const
    {
        for (size_t i = 0; i < _states.size(); ++i)
        {
            if (_states[i] == state) { return i; }
        }
        return _states.size();
    }

    template <class T>
    size_t LazyReusablePool<T>::indexOf(const T& object) const
    {
        const T* address = &object;
        const T* begin = _objects.data();
        const T* end = begin + _objects.size();
        const std::less<const T*> before;
        const bool is_inside = !before(address, begin) && before(address, end);
        if (!is_inside) { return _objects.size(); }
        return static_cast<size_t>(address - begin);
    }
}
