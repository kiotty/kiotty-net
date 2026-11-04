#if !defined(KIOTTY_REUSABLE_H)
#define KIOTTY_REUSABLE_H

#include "core/pool/kiotty_pool_handle.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace kiotty
{
    enum class SlotState : uint8_t
    {
        EMPTY,
        ACTIVE,
        RETIRED
    };

    namespace detail
    {
        template <class... Types>
        struct Voider
        {
            typedef void type;
        };

        template <class T, class = void>
        struct HasInitialize : std::false_type
        {
        };

        template <class T>
        struct HasInitialize<T, typename Voider<typename T::code_type, typename T::configuration_type, decltype(std::declval<T&>().initialize(std::declval<PoolHandle>(), std::declval<typename T::configuration_type>()))>::type>
            : std::is_same<decltype(std::declval<T&>().initialize(std::declval<PoolHandle>(), std::declval<typename T::configuration_type>())), typename T::code_type>
        {
        };

        template <class T, class = void>
        struct HasReset : std::false_type
        {
        };

        template <class T>
        struct HasReset<T, typename Voider<decltype(std::declval<T&>().reset())>::type>
            : std::integral_constant<bool, std::is_same<decltype(std::declval<T&>().reset()), void>::value && noexcept(std::declval<T&>().reset())>
        {
        };

        inline int32_t nextGeneration(int32_t generation)
        {
            if (generation == INT32_MAX)
            {
                return 1;
            }
            return generation + 1;
        }
    }

    template <class T>
    struct is_reusable
    {
        static const bool value =
            std::is_default_constructible<T>::value &&
            !std::is_copy_constructible<T>::value &&
            !std::is_copy_assignable<T>::value &&
            !std::is_move_constructible<T>::value &&
            !std::is_move_assignable<T>::value &&
            detail::HasInitialize<T>::value &&
            detail::HasReset<T>::value;
    };

    template <class T>
    const bool is_reusable<T>::value;
}

#endif
