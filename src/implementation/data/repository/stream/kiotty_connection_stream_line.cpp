#include "data/repository/stream/kiotty_connection_stream.h"

#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"

namespace kiotty::net::implementation
{
    namespace
    {
        Span<const char> stagedOf(StageBase& stage);
        size_t           findLineFeed(Span<const char> staged, size_t from);
        Span<const char> lineBefore(Span<const char> staged, size_t line_feed);
    }

    LineResult ConnectionStream::readLine(uint32_t timeout_milliseconds)
    {
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS) { return LineResult::failure(flushed); }
        size_t searched = 0;
        for (;;)
        {
            const Span<const char> staged = stagedOf(_receive);
            const size_t line_feed = findLineFeed(staged, searched);
            if (line_feed < staged.length)
            {
                _receive.consumer().commit(line_feed + 1);
                return LineResult::success(lineBefore(staged, line_feed));
            }
            searched = staged.length;
            const KiottyNetCode filled = fillStage(deadline);
            if (filled != KIOTTY_NET_CODE_SUCCESS) { return LineResult::failure(filled); }
        }
    }

    KiottyNetCode ConnectionStream::fill(uint32_t timeout_milliseconds)
    {
        const Deadline deadline(timeout_milliseconds);
        const KiottyNetCode flushed = flush(deadline);
        if (flushed != KIOTTY_NET_CODE_SUCCESS) { return flushed; }
        return fillStage(deadline);
    }

    KiottyNetCode ConnectionStream::fillStage(const Deadline& deadline)
    {
        _receive.compact();
        StageBase::Provider provider = _receive.provider();
        const size_t writable = provider.writable();
        const size_t room = writable < RESULT_MAXIMUM_BYTES ? writable : RESULT_MAXIMUM_BYTES;
        if (room == 0) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const Span<char> space{ provider.prepare(room), room };
        for (;;)
        {
            const KiottyNetResult received = readOnce(space, deadline);
            if (received.code != KIOTTY_NET_CODE_SUCCESS) { return received.code; }
            if (received.bytes > 0)
            {
                provider.commit(static_cast<size_t>(received.bytes));
                return KIOTTY_NET_CODE_SUCCESS;
            }
        }
    }

    namespace
    {
        Span<const char> stagedOf(StageBase& stage)
        {
            StageBase::Consumer consumer = stage.consumer();
            return Span<const char>{ consumer.peek(), consumer.readable() };
        }

        size_t findLineFeed(Span<const char> staged, size_t from)
        {
            for (size_t i = from; i < staged.length; ++i)
            {
                if (staged[i] == LINE_FEED) { return i; }
            }
            return staged.length;
        }

        Span<const char> lineBefore(Span<const char> staged, size_t line_feed)
        {
            const bool has_return = line_feed > 0 && staged[line_feed - 1] == CARRIAGE_RETURN;
            const size_t length = has_return ? line_feed - 1 : line_feed;
            return Span<const char>{ staged.data, length };
        }
    }
}
