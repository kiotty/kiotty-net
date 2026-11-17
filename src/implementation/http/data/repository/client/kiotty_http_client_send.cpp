#include "http/data/repository/client/kiotty_http_client_send.h"

#include "http/data/repository/request/kiotty_http_request_head_builder.h"

namespace kiotty::net::implementation
{
    TimeLeftResult timeLeftOf(const Deadline& deadline)
    {
        const uint32_t left = deadline.remaining();
        const bool is_expired = deadline.limited() && left == 0;
        if (is_expired) { return TimeLeftResult::failure(KIOTTY_NET_CODE_TIMEOUT); }
        return TimeLeftResult::success(left);
    }

    void settleStop(StreamClient& line)
    {
        if (line.stop().accept()) { line.disconnect(); }
    }

    HeadBytesResult writeHead(HttpClientState& state,
                              const ClientRequest& request,
                              uint64_t body_length)
    {
        const RequestHead head{ state.shared.list(), request.headers,  request.path,
                                state.target.host.span(), body_length, state.max_headers,
                                request.method,     request.type };
        const HeadLengthResult written = writeRequestHead(state.head_area, head);
        if (!written)
        {
            state.head_area.fill('\0');
            return HeadBytesResult::failure(written.code);
        }
        return HeadBytesResult::success(Span<const char>{ state.head_area.data, written.value });
    }

    Connected ensureConnected(StreamClient& line,
                              const HttpClientTarget& target,
                              const Deadline& deadline)
    {
        const bool is_open = static_cast<bool>(line.stream());
        if (is_open) { return Connected{ KIOTTY_NET_CODE_SUCCESS, true }; }
        return Connected{ line.reconnect(target.address, deadline), false };
    }

    KiottyNetCode writeAll(StreamClient& line, Span<const char> data, const Deadline& deadline)
    {
        const TimeLeftResult left = timeLeftOf(deadline);
        if (!left) { return left.code; }
        const StreamResult stream = line.stream();
        if (!stream) { return stream.code; }
        const KiottyNetSpan bytes{ data.data, data.length };
        return stream.value().write(bytes, left.value).code;
    }

    KiottyNetCode sendBytes(StreamClient& line,
                            Span<const char> head,
                            Span<const char> body,
                            const Deadline& deadline)
    {
        const KiottyNetCode sent = writeAll(line, head, deadline);
        if (sent != KIOTTY_NET_CODE_SUCCESS || body.empty()) { return sent; }
        return writeAll(line, body, deadline);
    }
}
