#pragma once

#include "core/pool/kiotty_reusable.h"
#include "core/pool/kiotty_reusable_pool.h"
#include "core/kiotty_span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::net::implementation
{
    template <class T>
    struct SizedReusablePoolStorage
    {
        SizedReusablePoolStorage();

        std::vector<T>         objects;
        std::vector<SlotState> states;
        std::vector<int32_t>   generations;
    };

    template <class T>
    class SizedReusablePool : private SizedReusablePoolStorage<T>, public ReusablePoolBase<T>
    {
    public:
        SizedReusablePool();

        bool resize(size_t slots);
    };

    template <class T>
    SizedReusablePoolStorage<T>::SizedReusablePoolStorage()
        : objects()
        , states()
        , generations()
    {
    }

    template <class T>
    SizedReusablePool<T>::SizedReusablePool()
        : SizedReusablePoolStorage<T>()
        , ReusablePoolBase<T>(Span<T>{ nullptr, 0 },
                              Span<SlotState>{ nullptr, 0 },
                              Span<int32_t>{ nullptr, 0 })
    {
    }

    template <class T>
    bool SizedReusablePool<T>::resize(size_t slots)
    {
        if (this->count() != 0) { return false; }
        if (slots == 0 || slots > REUSABLE_POOL_MAX_SLOTS) { return false; }
        if (slots <= this->objects.size()) { return true; }
        this->objects = std::vector<T>(slots);
        this->states.assign(slots, SlotState::EMPTY);
        this->generations.resize(slots, 0);
        this->bind(Span<T>{ this->objects.data(), slots },
                   Span<SlotState>{ this->states.data(), slots },
                   Span<int32_t>{ this->generations.data(), slots });
        return true;
    }
}
