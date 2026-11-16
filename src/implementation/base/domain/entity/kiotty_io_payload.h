#pragma once

#include <cstddef>

namespace kiotty::net::implementation
{
    struct IoPiece
    {
        void*  buffer;
        size_t length;
    };

    static_assert(sizeof(IoPiece) == sizeof(void*) + sizeof(size_t),
                  "IoPiece: unexpected padding");

    struct IoPayload
    {
        IoPiece first;
        IoPiece second;

        static IoPayload single(void* buffer, size_t length);
        static IoPayload pair(IoPiece first, IoPiece second);
        static IoPayload empty();

        size_t total() const;
    };

    static_assert(sizeof(IoPayload) == 2 * sizeof(IoPiece), "IoPayload: unexpected padding");

    inline IoPayload IoPayload::single(void* buffer, size_t length)
    {
        return IoPayload{ IoPiece{ buffer, length }, IoPiece{ nullptr, 0 } };
    }

    inline IoPayload IoPayload::pair(IoPiece first, IoPiece second)
    {
        if (first.length == 0)
        {
            return IoPayload{ second, IoPiece{ nullptr, 0 } };
        }
        return IoPayload{ first, second };
    }

    inline IoPayload IoPayload::empty()
    {
        return single(nullptr, 0);
    }

    inline size_t IoPayload::total() const
    {
        return first.length + second.length;
    }
}
