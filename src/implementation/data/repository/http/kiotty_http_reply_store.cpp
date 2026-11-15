#include "data/repository/http/kiotty_http_reply_store.h"

#include "domain/usecases/http/kiotty_http_header_line.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr StatusLine NO_STATUS_LINE{ 0, HttpVersion::V11 };
    }

    HttpReplyStore::HttpReplyStore(Span<char> header_buffer, Span<KiottyNetHttpHeader> headers)
        : _header_buffer(header_buffer)
        , _headers(headers)
        , _header_length(0)
        , _header_count(0)
        , _status_line(NO_STATUS_LINE)
    {
    }

    void HttpReplyStore::begin()
    {
        _header_length = 0;
        _header_count = 0;
        _status_line = NO_STATUS_LINE;
    }

    KiottyNetCode HttpReplyStore::takeStatusLine(Span<const char> line)
    {
        const size_t room = _header_buffer.length - _header_length;
        if (line.length > room) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const StatusLineResult parsed = parseStatusLine(line);
        if (!parsed) { return parsed.code; }
        _header_length += line.length;
        _status_line = parsed.value;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpReplyStore::takeHeader(Span<const char> line)
    {
        const bool is_full = _header_count >= _headers.length;
        if (is_full) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const Span<char> copied = claimHeaderRoom(line);
        if (copied.length != line.length) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const Span<const char> text{ copied.data, copied.length };
        const HeaderResult parsed = parseHeaderLine(text, text.length);
        if (!parsed) { return KIOTTY_NET_CODE_PROTOCOL_ERROR; }
        _headers[_header_count] = parsed.value;
        _header_count += 1;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    ReplyFramingResult HttpReplyStore::finish(KiottyNetHttpMethod method) const
    {
        return selectReplyFraming(method, _status_line, headers());
    }

    int32_t HttpReplyStore::status() const
    {
        return _status_line.status;
    }

    Span<const KiottyNetHttpHeader> HttpReplyStore::headers() const
    {
        return Span<const KiottyNetHttpHeader>{ _headers.data, _header_count };
    }

    Span<char> HttpReplyStore::claimHeaderRoom(Span<const char> line)
    {
        const size_t room = _header_buffer.length - _header_length;
        if (line.length > room) { return Span<char>{ nullptr, 0 }; }
        const Span<char> target{ _header_buffer.data + _header_length, line.length };
        (void)target.copyFrom(line);
        _header_length += line.length;
        return target;
    }
}
