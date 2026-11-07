#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable.h"
#include "core/kiotty_result.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace kiotty::net::implementation
{
    template <class T, size_t N>
    class LazyReusablePool
    {
        static_assert(is_reusable<T>::value, "LazyReusablePool needs a default constructible, non copyable, non movable T with initialize and noexcept reset");
        static_assert(N > 0, "a pool needs at least one slot");

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
        code_type     reserve(size_t count);
        void          clear();
        size_t        capacity() const;
        size_t        count() const;
        size_t        created() const;

    private:
        size_t retiredSlot() const;
        size_t emptySlot() const;
        size_t indexOf(const T& object) const;
        bool   create(size_t index);

        std::array<std::unique_ptr<T>, N> _objects;
        std::array<SlotState, N>          _states;
        std::array<int32_t, N>            _generations;
        size_t                            _count;
    };

    template <class T, size_t N>
    LazyReusablePool<T, N>::LazyReusablePool()
        : _objects()
        , _states()
        , _generations()
        , _count(0)
    {
        _states.fill(SlotState::EMPTY);
        _generations.fill(0);
    }

    template <class T, size_t N>
    LazyReusablePool<T, N>::~LazyReusablePool()
    {
        clear();
    }

    template <class T, size_t N>
    typename LazyReusablePool<T, N>::AcquireResult LazyReusablePool<T, N>::acquire(configuration_type configuration)
    {
        size_t index = retiredSlot();
        if (index == N)
        {
            index = emptySlot();
            if (index == N) { return AcquireResult::failure(static_cast<code_type>(T::TABLE_FULL)); }
            const bool made = create(index);
            if (!made) { return AcquireResult::failure(static_cast<code_type>(T::OUT_OF_MEMORY)); }
        }
        _generations[index] = detail::nextGeneration(_generations[index]);
        const PoolHandle id{ _generations[index], static_cast<int32_t>(index) };
        T& object = *_objects[index];
        const code_type code = object.initialize(id, std::move(configuration));
        if (code != code_type{})
        {
            object.reset();
            return AcquireResult::failure(code);
        }
        _states[index] = SlotState::ACTIVE;
        _count += 1;
        return AcquireResult::success(object);
    }

    template <class T, size_t N>
    void LazyReusablePool<T, N>::release(T& object)
    {
        const size_t index = indexOf(object);
        assert(index < N && _states[index] == SlotState::ACTIVE);
        if (index >= N || _states[index] != SlotState::ACTIVE) { return; }
        object.reset();
        _states[index] = SlotState::RETIRED;
        _count -= 1;
    }

    template <class T, size_t N>
    typename LazyReusablePool<T, N>::code_type LazyReusablePool<T, N>::reserve(size_t count)
    {
        while (created() < count)
        {
            const size_t index = emptySlot();
            if (index == N) { return static_cast<code_type>(T::TABLE_FULL); }
            const bool made = create(index);
            if (!made) { return static_cast<code_type>(T::OUT_OF_MEMORY); }
        }
        return code_type{};
    }

    template <class T, size_t N>
    void LazyReusablePool<T, N>::clear()
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (_states[i] == SlotState::ACTIVE)
            {
                _objects[i]->reset();
            }
            _objects[i].reset();
            _states[i] = SlotState::EMPTY;
        }
        _count = 0;
    }

    template <class T, size_t N>
    size_t LazyReusablePool<T, N>::capacity() const
    {
        return N;
    }

    template <class T, size_t N>
    size_t LazyReusablePool<T, N>::count() const
    {
        return _count;
    }

    template <class T, size_t N>
    size_t LazyReusablePool<T, N>::created() const
    {
        size_t total = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (_states[i] != SlotState::EMPTY)
            {
                total += 1;
            }
        }
        return total;
    }

    template <class T, size_t N>
    size_t LazyReusablePool<T, N>::retiredSlot() const
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (_states[i] == SlotState::RETIRED) { return i; }
        }
        return N;
    }

    template <class T, size_t N>
    size_t LazyReusablePool<T, N>::emptySlot() const
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (_states[i] == SlotState::EMPTY) { return i; }
        }
        return N;
    }

    template <class T, size_t N>
    size_t LazyReusablePool<T, N>::indexOf(const T& object) const
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (_objects[i].get() == &object) { return i; }
        }
        return N;
    }

    template <class T, size_t N>
    bool LazyReusablePool<T, N>::create(size_t index)
    {
        _objects[index].reset(new (std::nothrow) T);
        if (!_objects[index]) { return false; }
        _states[index] = SlotState::RETIRED;
        return true;
    }
}
