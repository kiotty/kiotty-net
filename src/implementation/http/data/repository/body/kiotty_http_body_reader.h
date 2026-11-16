#pragma once

#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_body_step.h"
#include "http/domain/entity/kiotty_http_reply_framing.h"
#include "kiotty_net_base.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class HttpBodyReader
    {
    public:
        HttpBodyReader();
        explicit HttpBodyReader(const ReplyFraming& framing);

        BodyStep      take(Span<const char> input, Span<char> output);
        KiottyNetCode closeInput();
        bool          isFinished() const;

    private:
        struct Moved
        {
            size_t consumed;
            size_t produced;
        };

        Moved     move(Span<const char> rest, Span<char> room);
        Moved     moveAll(Span<const char> rest, Span<char> room);
        Moved     moveCounted(Span<const char> rest, Span<char> room);
        BodyState takeSizeByte(char byte);
        BodyState endSizeLine();

        std::array<char, CHUNK_LINE_BYTES> _line;
        uint64_t                           _remaining;
        size_t                             _line_length;
        BodyState                          _state;
    };

    static_assert(sizeof(HttpBodyReader) <= CHUNK_LINE_BYTES + 3 * sizeof(uint64_t),
                  "HttpBodyReader: unexpected padding");
}
