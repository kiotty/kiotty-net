#pragma once

#include "core/pool/kiotty_pool_handle.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace kiotty::net::implementation
{
    enum class SlotState : uint8_t
    {
        EMPTY,
        ACTIVE,
        RETIRED
    };

    namespace detail
    {
        template <class T, class = void>
        struct HasInitialize : std::false_type
        {
        };

        template <class T>
        using InitializeCall = decltype(std::declval<T&>().initialize(
            std::declval<PoolHandle>(), std::declval<typename T::configuration_type>()));

        template <class T>
        struct HasInitialize<T, std::void_t<typename T::code_type, InitializeCall<T>>>
            : std::is_same<InitializeCall<T>, typename T::code_type>
        {
        };

        template <class T, class = void>
        struct HasReset : std::false_type
        {
        };

        template <class T>
        using ResetCall = decltype(std::declval<T&>().reset());

        template <class T>
        struct HasReset<T, std::void_t<ResetCall<T>>>
            : std::bool_constant<std::is_same<ResetCall<T>, void>::value
                                 && noexcept(std::declval<T&>().reset())>
        {
        };

        inline int32_t nextGeneration(int32_t generation)
        {
            if (generation == INT32_MAX) { return 1; }
            return generation + 1;
        }
    }

    template <class T>
    struct is_reusable
    {
        static constexpr bool value =
            std::is_default_constructible<T>::value &&
            !std::is_copy_constructible<T>::value &&
            !std::is_copy_assignable<T>::value &&
            !std::is_move_constructible<T>::value &&
            !std::is_move_assignable<T>::value &&
            detail::HasInitialize<T>::value &&
            detail::HasReset<T>::value;
    };
}
