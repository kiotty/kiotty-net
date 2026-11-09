#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "core/kiotty_result.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace kiotty::net::implementation
{
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

        AcquireResult acquire(configuration_type configuration);
        void          release(T& object);
        void          clear();
        size_t        capacity() const;
        size_t        count() const;

        template <class Visit>
        void forEachActive(Visit visit);

    private:
        size_t findSlot(SlotState state) const;
        size_t indexOf(const T& object) const;
        size_t grow();

        std::vector<std::unique_ptr<T>> _objects;
        std::vector<SlotState>          _states;
        std::vector<int32_t>            _generations;
        size_t                          _count;
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
    typename LazyReusablePool<T>::AcquireResult
    LazyReusablePool<T>::acquire(configuration_type configuration)
    {
        const size_t none = _objects.size();
        size_t index = findSlot(SlotState::RETIRED);
        if (index == none) { index = findSlot(SlotState::EMPTY); }
        if (index == none) { index = grow(); }
        const code_type full = static_cast<code_type>(T::TABLE_FULL);
        if (index >= _objects.size()) { return AcquireResult::failure(full); }
        _generations[index] = detail::nextGeneration(_generations[index]);
        const PoolHandle id{ _generations[index], static_cast<int32_t>(index) };
        T& object = *_objects[index];
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
            if (_states[i] == SlotState::ACTIVE) { _objects[i]->reset(); }
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
    template <class Visit>
    void LazyReusablePool<T>::forEachActive(Visit visit)
    {
        for (size_t i = 0; i < _objects.size(); ++i)
        {
            if (_states[i] == SlotState::ACTIVE) { visit(*_objects[i]); }
        }
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
        for (size_t i = 0; i < _objects.size(); ++i)
        {
            if (_objects[i].get() == &object) { return i; }
        }
        return _objects.size();
    }

    template <class T>
    size_t LazyReusablePool<T>::grow()
    {
        const size_t index = _objects.size();
        if (index >= REUSABLE_POOL_MAX_SLOTS) { return index; }
        _objects.push_back(std::make_unique<T>());
        _states.push_back(SlotState::EMPTY);
        _generations.push_back(0);
        return index;
    }
}
