#pragma once

#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace kiotty::net::implementation
{
    static constexpr size_t REUSABLE_POOL_MAX_SLOTS = 65536;

    template <class T>
    class ReusablePoolBase
    {
        static_assert(is_reusable<T>::value,
                      "ReusablePool needs a default constructible, non copyable, non movable T "
                      "with initialize and noexcept reset");

    public:
        using code_type = typename T::code_type;
        using configuration_type = typename T::configuration_type;
        using AcquireResult = Result<T&, code_type>;
        using ConstResult = Result<const T&, code_type>;

        ReusablePoolBase(const ReusablePoolBase&) = delete;
        ReusablePoolBase& operator=(const ReusablePoolBase&) = delete;

        AcquireResult acquire(configuration_type configuration);
        void          release(T& object);
        AcquireResult get(PoolHandle id);
        ConstResult   get(PoolHandle id) const;
        T&            at(size_t index);
        const T&      at(size_t index) const;
        bool          active(size_t index) const;
        size_t        capacity() const;
        size_t        count() const;

    protected:
        ReusablePoolBase(Span<T> objects, Span<SlotState> states, Span<int32_t> generations);
        ~ReusablePoolBase();

    private:
        size_t indexOf(const T& object) const;
        size_t slotOf(PoolHandle id) const;

        Span<T>         _objects;
        Span<SlotState> _states;
        Span<int32_t>   _generations;
        size_t          _count;
    };

    template <class T, size_t N>
    struct ReusablePoolStorage
    {
        ReusablePoolStorage();

        std::array<T, N>         objects;
        std::array<SlotState, N> states;
        std::array<int32_t, N>   generations;
    };

    template <class T, size_t N>
    class ReusablePool : private ReusablePoolStorage<T, N>, public ReusablePoolBase<T>
    {
        static_assert(N > 0, "a pool needs at least one slot");
        static_assert(N <= REUSABLE_POOL_MAX_SLOTS, "a slot index must fit 16 bits");

    public:
        ReusablePool();
    };

    template <class T>
    ReusablePoolBase<T>::ReusablePoolBase(Span<T> objects,
                                          Span<SlotState> states,
                                          Span<int32_t> generations)
        : _objects(objects)
        , _states(states)
        , _generations(generations)
        , _count(0)
    {
    }

    template <class T>
    ReusablePoolBase<T>::~ReusablePoolBase()
    {
        for (size_t i = 0; i < _objects.length; ++i)
        {
            if (_states[i] == SlotState::ACTIVE)
            {
                _objects[i].reset();
            }
        }
    }

    template <class T>
    typename ReusablePoolBase<T>::AcquireResult
    ReusablePoolBase<T>::acquire(configuration_type configuration)
    {
        for (size_t i = 0; i < _objects.length; ++i)
        {
            if (_states[i] != SlotState::EMPTY) { continue; }
            _generations[i] = detail::nextGeneration(_generations[i]);
            const PoolHandle id{ _generations[i], static_cast<int32_t>(i) };
            const code_type code = _objects[i].initialize(id, std::move(configuration));
            if (code != code_type{})
            {
                _objects[i].reset();
                return AcquireResult::failure(code);
            }
            _states[i] = SlotState::ACTIVE;
            _count += 1;
            return AcquireResult::success(_objects[i]);
        }
        return AcquireResult::failure(static_cast<code_type>(T::TABLE_FULL));
    }

    template <class T>
    void ReusablePoolBase<T>::release(T& object)
    {
        const size_t index = indexOf(object);
        assert(index < _objects.length && _states[index] == SlotState::ACTIVE);
        if (index >= _objects.length || _states[index] != SlotState::ACTIVE) { return; }
        _objects[index].reset();
        _states[index] = SlotState::EMPTY;
        _count -= 1;
    }

    template <class T>
    typename ReusablePoolBase<T>::AcquireResult ReusablePoolBase<T>::get(PoolHandle id)
    {
        const size_t index = slotOf(id);
        const code_type invalid = static_cast<code_type>(T::INVALID_HANDLE);
        if (index == _objects.length) { return AcquireResult::failure(invalid); }
        return AcquireResult::success(_objects[index]);
    }

    template <class T>
    typename ReusablePoolBase<T>::ConstResult ReusablePoolBase<T>::get(PoolHandle id) const
    {
        const size_t index = slotOf(id);
        const code_type invalid = static_cast<code_type>(T::INVALID_HANDLE);
        if (index == _objects.length) { return ConstResult::failure(invalid); }
        return ConstResult::success(_objects[index]);
    }

    template <class T>
    size_t ReusablePoolBase<T>::slotOf(PoolHandle id) const
    {
        if (id.index < 0) { return _objects.length; }
        const size_t index = static_cast<size_t>(id.index);
        if (index >= _objects.length) { return _objects.length; }
        const bool is_active = _states[index] == SlotState::ACTIVE;
        const bool is_current = _generations[index] == id.generation;
        if (!is_active || !is_current) { return _objects.length; }
        return index;
    }

    template <class T>
    T& ReusablePoolBase<T>::at(size_t index)
    {
        return _objects[index];
    }

    template <class T>
    const T& ReusablePoolBase<T>::at(size_t index) const
    {
        return _objects[index];
    }

    template <class T>
    bool ReusablePoolBase<T>::active(size_t index) const
    {
        return index < _objects.length && _states[index] == SlotState::ACTIVE;
    }

    template <class T>
    size_t ReusablePoolBase<T>::capacity() const
    {
        return _objects.length;
    }

    template <class T>
    size_t ReusablePoolBase<T>::count() const
    {
        return _count;
    }

    template <class T>
    size_t ReusablePoolBase<T>::indexOf(const T& object) const
    {
        const T* address = &object;
        const T* begin = _objects.data;
        const T* end = begin + _objects.length;
        const std::less<const T*> before;
        const bool is_inside = !before(address, begin) && before(address, end);
        if (!is_inside) { return _objects.length; }
        return static_cast<size_t>(address - begin);
    }

    template <class T, size_t N>
    ReusablePoolStorage<T, N>::ReusablePoolStorage()
        : objects()
        , states()
        , generations()
    {
        states.fill(SlotState::EMPTY);
        generations.fill(0);
    }

    template <class T, size_t N>
    ReusablePool<T, N>::ReusablePool()
        : ReusablePoolStorage<T, N>()
        , ReusablePoolBase<T>(Span<T>{ this->objects.data(), N },
                              Span<SlotState>{ this->states.data(), N },
                              Span<int32_t>{ this->generations.data(), N })
    {
    }
}
