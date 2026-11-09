#pragma once

#include "core/kiotty_span.h"

#include <array>
#include <cstddef>

namespace kiotty::net::implementation
{
    template <size_t N>
    class FixedString
    {
        static_assert(N > 0, "FixedString: N counts the terminator and must be positive");

    public:
        static constexpr size_t MAX_LENGTH = N - 1;

        FixedString();

        bool             assign(Span<const char> text);
        bool             append(Span<const char> text);
        Span<const char> span() const;
        const char*      c_str() const;
        size_t           length() const;
        bool             empty() const;
        void             clear();

    private:
        std::array<char, N> _text;
        size_t              _length;
    };

    template <size_t N>
    inline FixedString<N>::FixedString()
        : _text{}
        , _length(0)
    {
    }

    template <size_t N>
    inline bool FixedString<N>::assign(Span<const char> text)
    {
        clear();
        if (text.length > MAX_LENGTH) { return false; }
        const Span<char> target{ _text.data(), MAX_LENGTH };
        const bool copied = target.copyFrom(text);
        if (!copied) { return false; }
        _length = text.length;
        _text[_length] = '\0';
        return true;
    }

    template <size_t N>
    inline bool FixedString<N>::append(Span<const char> text)
    {
        const size_t room = MAX_LENGTH - _length;
        if (text.length > room) { return false; }
        const Span<char> target{ _text.data() + _length, room };
        const bool copied = target.copyFrom(text);
        if (!copied) { return false; }
        _length += text.length;
        _text[_length] = '\0';
        return true;
    }

    template <size_t N>
    inline Span<const char> FixedString<N>::span() const
    {
        return Span<const char>{ _text.data(), _length };
    }

    template <size_t N>
    inline const char* FixedString<N>::c_str() const
    {
        return _text.data();
    }

    template <size_t N>
    inline size_t FixedString<N>::length() const
    {
        return _length;
    }

    template <size_t N>
    inline bool FixedString<N>::empty() const
    {
        return _length == 0;
    }

    template <size_t N>
    inline void FixedString<N>::clear()
    {
        _length = 0;
        _text[0] = '\0';
    }
}
