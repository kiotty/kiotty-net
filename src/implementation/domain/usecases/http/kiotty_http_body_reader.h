#pragma once

#include "core/kiotty_span.h"
#include "domain/usecases/http/kiotty_http_reply_framing.h"
#include "kiotty_net_base.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    inline constexpr size_t CHUNK_LINE_BYTES = 256;

    struct BodyStep
    {
        size_t        consumed;
        size_t        produced;
        KiottyNetCode code;
        bool          is_finished;
    };

    static_assert(sizeof(BodyStep) == 2 * sizeof(size_t) + 2 * sizeof(int32_t),
                  "BodyStep: unexpected padding");

    enum class BodyState : uint8_t
    {
        DONE,
        FAILED,
        CUT_SHORT,
        LENGTH,
        UNTIL_CLOSE,
        CHUNK_SIZE,
        CHUNK_DATA,
        CHUNK_DATA_END,
        CHUNK_DATA_LINE_FEED,
        TRAILER_LINE_START,
        TRAILER_LINE_REST
    };

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
