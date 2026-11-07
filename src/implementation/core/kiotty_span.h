#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace kiotty::net::implementation
{
    template <typename T>
    struct Span
    {
        T*     data;
        size_t length;

        T*      begin() const;
        T*      end() const;
        bool    empty() const;
        T&      operator[](size_t index) const;
        Span<T> skip(size_t count) const;
        void    fill(const T& value) const;

        template <typename Source>
        bool copyFrom(Span<Source> source) const;
    };

    template <typename T>
    inline T* Span<T>::begin() const
    {
        return data;
    }

    template <typename T>
    inline T* Span<T>::end() const
    {
        return data + length;
    }

    template <typename T>
    inline bool Span<T>::empty() const
    {
        return length == 0;
    }

    template <typename T>
    inline T& Span<T>::operator[](size_t index) const
    {
        return data[index];
    }

    template <typename T>
    inline Span<T> Span<T>::skip(size_t count) const
    {
        if (count >= length) { return Span<T>{ data + length, 0 }; }
        return Span<T>{ data + count, length - count };
    }

    template <typename T>
    inline void Span<T>::fill(const T& value) const
    {
        for (size_t i = 0; i < length; ++i)
        {
            data[i] = value;
        }
    }

    template <typename T>
    template <typename Source>
    inline bool Span<T>::copyFrom(Span<Source> source) const
    {
        static_assert(std::is_same<typename std::remove_const<T>::type, typename std::remove_const<Source>::type>::value, "Span::copyFrom: element types must match");
        static_assert(std::is_trivially_copyable<T>::value, "Span::copyFrom: elements must be trivially copyable");
        if (source.length > length) { return false; }
        if (source.length == 0) { return true; }
        std::memcpy(data, source.data, source.length * sizeof(T));
        return true;
    }
}
