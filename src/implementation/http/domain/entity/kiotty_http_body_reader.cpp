#include "http/domain/entity/kiotty_http_body_reader.h"

#include "core/kiotty_text.h"

#include <algorithm>

namespace kiotty::net::implementation
{
    namespace
    {
        BodyState        initialStateOf(const ReplyFraming& framing);
        bool             isMoving(BodyState state, size_t input_left, size_t output_left);
        bool             isSettled(BodyState state);
        bool             isPassingData(BodyState state);
        BodyState        nextLineState(BodyState state, char byte);
        BodyState        afterChunkData(char byte);
        BodyState        afterTrailerStart(char byte);
        KiottyNetCode    codeOf(BodyState state);
        Span<const char> withoutCarriageReturn(Span<const char> line);
    }

    HttpBodyReader::HttpBodyReader()
        : _line{}
        , _remaining(0)
        , _line_length(0)
        , _state(BodyState::DONE)
    {
    }

    HttpBodyReader::HttpBodyReader(const ReplyFraming& framing)
        : _line{}
        , _remaining(framing.length)
        , _line_length(0)
        , _state(initialStateOf(framing))
    {
    }

    BodyStep HttpBodyReader::take(Span<const char> input, Span<char> output)
    {
        size_t consumed = 0;
        size_t produced = 0;
        while (isMoving(_state, input.length - consumed, output.length - produced))
        {
            const Moved moved = move(input.skip(consumed), output.skip(produced));
            consumed += moved.consumed;
            produced += moved.produced;
        }
        return BodyStep{ consumed, produced, codeOf(_state), isFinished() };
    }

    KiottyNetCode HttpBodyReader::closeInput()
    {
        const bool is_body_end = _state == BodyState::UNTIL_CLOSE || _state == BodyState::DONE;
        const bool is_failed = _state == BodyState::FAILED;
        const BodyState cut = is_failed ? BodyState::FAILED : BodyState::CUT_SHORT;
        _state = is_body_end ? BodyState::DONE : cut;
        return is_body_end ? KIOTTY_NET_CODE_SUCCESS : KIOTTY_NET_CODE_PEER_CLOSED;
    }

    bool HttpBodyReader::isFinished() const
    {
        return _state == BodyState::DONE;
    }

    HttpBodyReader::Moved HttpBodyReader::move(Span<const char> rest, Span<char> room)
    {
        if (isPassingData(_state)) { return moveCounted(rest, room); }
        if (_state == BodyState::UNTIL_CLOSE) { return moveAll(rest, room); }
        const bool is_size_line = _state == BodyState::CHUNK_SIZE;
        _state = is_size_line ? takeSizeByte(rest[0]) : nextLineState(_state, rest[0]);
        return Moved{ 1, 0 };
    }

    HttpBodyReader::Moved HttpBodyReader::moveAll(Span<const char> rest, Span<char> room)
    {
        const size_t length = std::min(rest.length, room.length);
        (void)room.copyFrom(Span<const char>{ rest.data, length });
        return Moved{ length, length };
    }

    HttpBodyReader::Moved HttpBodyReader::moveCounted(Span<const char> rest, Span<char> room)
    {
        const size_t available = std::min(rest.length, room.length);
        const uint64_t allowed = std::min(_remaining, static_cast<uint64_t>(available));
        const size_t length = static_cast<size_t>(allowed);
        (void)room.copyFrom(Span<const char>{ rest.data, length });
        _remaining -= allowed;
        const bool is_chunk = _state == BodyState::CHUNK_DATA;
        const BodyState ended = is_chunk ? BodyState::CHUNK_DATA_END : BodyState::DONE;
        _state = _remaining == 0 ? ended : _state;
        return Moved{ length, length };
    }

    BodyState HttpBodyReader::takeSizeByte(char byte)
    {
        if (byte == LINE_FEED) { return endSizeLine(); }
        const bool is_full = _line_length >= _line.size();
        if (is_full) { return BodyState::FAILED; }
        _line[_line_length] = byte;
        _line_length += 1;
        return BodyState::CHUNK_SIZE;
    }

    BodyState HttpBodyReader::endSizeLine()
    {
        const Span<const char> line{ _line.data(), _line_length };
        _line_length = 0;
        const LengthResult size = parseChunkSize(withoutCarriageReturn(line));
        if (!size) { return BodyState::FAILED; }
        _remaining = size.value;
        return size.value == 0 ? BodyState::TRAILER_LINE_START : BodyState::CHUNK_DATA;
    }

    namespace
    {
        BodyState initialStateOf(const ReplyFraming& framing)
        {
            switch (framing.kind)
            {
            case ReplyBodyKind::LENGTH:
                return framing.length == 0 ? BodyState::DONE : BodyState::LENGTH;
            case ReplyBodyKind::CHUNKED:
                return BodyState::CHUNK_SIZE;
            case ReplyBodyKind::UNTIL_CLOSE:
                return BodyState::UNTIL_CLOSE;
            case ReplyBodyKind::NONE:
                break;
            }
            return BodyState::DONE;
        }

        bool isMoving(BodyState state, size_t input_left, size_t output_left)
        {
            if (isSettled(state)) { return false; }
            if (input_left == 0) { return false; }
            const bool is_writing = isPassingData(state) || state == BodyState::UNTIL_CLOSE;
            return output_left > 0 || !is_writing;
        }

        bool isSettled(BodyState state)
        {
            const bool is_ended = state == BodyState::DONE || state == BodyState::CUT_SHORT;
            return is_ended || state == BodyState::FAILED;
        }

        bool isPassingData(BodyState state)
        {
            return state == BodyState::LENGTH || state == BodyState::CHUNK_DATA;
        }

        BodyState nextLineState(BodyState state, char byte)
        {
            switch (state)
            {
            case BodyState::CHUNK_DATA_END:
                return afterChunkData(byte);
            case BodyState::CHUNK_DATA_LINE_FEED:
                return byte == LINE_FEED ? BodyState::CHUNK_SIZE : BodyState::FAILED;
            case BodyState::TRAILER_LINE_START:
                return afterTrailerStart(byte);
            case BodyState::TRAILER_LINE_REST:
                return byte == LINE_FEED ? BodyState::TRAILER_LINE_START : state;
            case BodyState::DONE:
            case BodyState::FAILED:
            case BodyState::CUT_SHORT:
            case BodyState::LENGTH:
            case BodyState::UNTIL_CLOSE:
            case BodyState::CHUNK_SIZE:
            case BodyState::CHUNK_DATA:
                break;
            }
            return BodyState::FAILED;
        }

        BodyState afterChunkData(char byte)
        {
            if (byte == CARRIAGE_RETURN) { return BodyState::CHUNK_DATA_LINE_FEED; }
            if (byte == LINE_FEED) { return BodyState::CHUNK_SIZE; }
            return BodyState::FAILED;
        }

        BodyState afterTrailerStart(char byte)
        {
            if (byte == LINE_FEED) { return BodyState::DONE; }
            if (byte == CARRIAGE_RETURN) { return BodyState::TRAILER_LINE_START; }
            return BodyState::TRAILER_LINE_REST;
        }

        KiottyNetCode codeOf(BodyState state)
        {
            if (state == BodyState::FAILED) { return KIOTTY_NET_CODE_PROTOCOL_ERROR; }
            if (state == BodyState::CUT_SHORT) { return KIOTTY_NET_CODE_PEER_CLOSED; }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        Span<const char> withoutCarriageReturn(Span<const char> line)
        {
            const bool has_return = line.length > 0 && line[line.length - 1] == CARRIAGE_RETURN;
            const size_t length = has_return ? line.length - 1 : line.length;
            return Span<const char>{ line.data, length };
        }
    }
}
