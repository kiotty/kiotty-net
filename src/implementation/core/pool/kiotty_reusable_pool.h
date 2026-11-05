#if !defined(KIOTTY_REUSABLE_POOL_H)
#define KIOTTY_REUSABLE_POOL_H

#include "core/kiotty_result.h"
#include "core/pool/kiotty_pool_handle.h"
#include "core/pool/kiotty_reusable.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace kiotty
{
    static const size_t REUSABLE_POOL_MAX_SLOTS = 65536;

    template <class T, size_t N>
    class ReusablePool
    {
        static_assert(is_reusable<T>::value, "ReusablePool needs a default constructible, non copyable, non movable T with initialize and noexcept reset");
        static_assert(N > 0, "a pool needs at least one slot");
        static_assert(N <= REUSABLE_POOL_MAX_SLOTS, "a slot index must fit 16 bits");

    public:
        typedef typename T::code_type          code_type;
        typedef typename T::configuration_type configuration_type;
        typedef Result<code_type, T&>          AcquireResult;

        ReusablePool();
        ~ReusablePool();

        ReusablePool(const ReusablePool&) = delete;
        ReusablePool& operator=(const ReusablePool&) = delete;

        AcquireResult acquire(configuration_type configuration);
        void          release(T& object);
        T*            find(PoolHandle id);
        const T*      find(PoolHandle id) const;
        T&            at(size_t index);
        const T&      at(size_t index) const;
        bool          active(size_t index) const;
        size_t        capacity() const;
        size_t        count() const;
        bool          full() const;
        bool          empty() const;

    private:
        size_t indexOf(const T& object) const;
        size_t slotOf(PoolHandle id) const;

        std::array<T, N>         _objects;
        std::array<SlotState, N> _states;
        std::array<int32_t, N>   _generations;
        size_t                   _count;
    };

    template <class T, size_t N>
    ReusablePool<T, N>::ReusablePool()
        : _states()
        , _generations()
        , _count(0)
    {
        _states.fill(SlotState::EMPTY);
        _generations.fill(0);
    }

    template <class T, size_t N>
    ReusablePool<T, N>::~ReusablePool()
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (_states[i] == SlotState::ACTIVE)
            {
                _objects[i].reset();
            }
        }
    }

    template <class T, size_t N>
    typename ReusablePool<T, N>::AcquireResult ReusablePool<T, N>::acquire(configuration_type configuration)
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (_states[i] != SlotState::EMPTY)
            {
                continue;
            }
            _generations[i] = detail::nextGeneration(_generations[i]);
            PoolHandle id;
            id.generation = _generations[i];
            id.index = static_cast<int32_t>(i);
            code_type code = _objects[i].initialize(id, std::move(configuration));
            if (code != static_cast<code_type>(0))
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

    template <class T, size_t N>
    void ReusablePool<T, N>::release(T& object)
    {
        size_t index = indexOf(object);
        assert(index < N && _states[index] == SlotState::ACTIVE);
        if (index >= N || _states[index] != SlotState::ACTIVE)
        {
            return;
        }
        _objects[index].reset();
        _states[index] = SlotState::EMPTY;
        _count -= 1;
    }

    template <class T, size_t N>
    T* ReusablePool<T, N>::find(PoolHandle id)
    {
        size_t index = slotOf(id);
        if (index == N)
        {
            return nullptr;
        }
        return &_objects[index];
    }

    template <class T, size_t N>
    const T* ReusablePool<T, N>::find(PoolHandle id) const
    {
        size_t index = slotOf(id);
        if (index == N)
        {
            return nullptr;
        }
        return &_objects[index];
    }

    template <class T, size_t N>
    size_t ReusablePool<T, N>::slotOf(PoolHandle id) const
    {
        if (id.index < 0)
        {
            return N;
        }
        size_t index = static_cast<size_t>(id.index);
        if (index >= N)
        {
            return N;
        }
        if (_states[index] != SlotState::ACTIVE || _generations[index] != id.generation)
        {
            return N;
        }
        return index;
    }

    template <class T, size_t N>
    T& ReusablePool<T, N>::at(size_t index)
    {
        return _objects[index];
    }

    template <class T, size_t N>
    const T& ReusablePool<T, N>::at(size_t index) const
    {
        return _objects[index];
    }

    template <class T, size_t N>
    bool ReusablePool<T, N>::active(size_t index) const
    {
        return index < N && _states[index] == SlotState::ACTIVE;
    }

    template <class T, size_t N>
    size_t ReusablePool<T, N>::capacity() const
    {
        return N;
    }

    template <class T, size_t N>
    size_t ReusablePool<T, N>::count() const
    {
        return _count;
    }

    template <class T, size_t N>
    bool ReusablePool<T, N>::full() const
    {
        return _count == N;
    }

    template <class T, size_t N>
    bool ReusablePool<T, N>::empty() const
    {
        return _count == 0;
    }

    template <class T, size_t N>
    size_t ReusablePool<T, N>::indexOf(const T& object) const
    {
        const T* address = &object;
        const T* begin = _objects.data();
        std::less<const T*> before;
        if (before(address, begin) || !before(address, begin + N))
        {
            return N;
        }
        return static_cast<size_t>(address - begin);
    }
}

#endif
