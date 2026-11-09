#if !defined(KIOTTY_TEST_TEXT_H)
#define KIOTTY_TEST_TEXT_H

#include "core/kiotty_span.h"

#include <cstring>

namespace kiotty_test
{
    inline kiotty::net::implementation::Span<const char> textOf(const char* text)
    {
        if (text == nullptr)
        {
            return kiotty::net::implementation::Span<const char>{ nullptr, 0 };
        }
        return kiotty::net::implementation::Span<const char>{ text, std::strlen(text) };
    }
}

#endif
