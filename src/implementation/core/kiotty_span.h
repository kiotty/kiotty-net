#if !defined(KIOTTY_SPAN_H)
#define KIOTTY_SPAN_H

#include <cstddef>

namespace kiotty
{
    template <typename T>
    struct Span
    {
        T*     data;
        size_t length;

        T*   begin() const;
        T*   end() const;
        bool empty() const;
        T&   operator[](size_t index) const;
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
}

#endif
