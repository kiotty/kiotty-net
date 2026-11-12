#include "data/repository/http/kiotty_http_request_store.h"

#include "domain/usecases/http/kiotty_http_header_line.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr KiottyNetHttpStatus TOO_LARGE = KIOTTY_NET_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE;
        constexpr KiottyNetHttpStatus CONTENT_TOO_LARGE = KIOTTY_NET_HTTP_STATUS_CONTENT_TOO_LARGE;

        KiottyNetSpan spanOf(Span<const char> text);
    }

    HttpRequestStore::HttpRequestStore(Span<char> header_buffer,
                                       Span<KiottyNetHttpHeader> headers,
                                       Span<char> body_buffer)
        : _request{}
        , _header_buffer(header_buffer)
        , _headers(headers)
        , _body_buffer(body_buffer)
        , _header_length(0)
        , _version(HttpVersion::V11)
    {
    }

    void HttpRequestStore::begin(KiottyNetConnectionHandle connection)
    {
        _request = KiottyNetHttpRequest{};
        _request.connection = connection;
        _request.headers = _headers.data;
        _request.body = KiottyNetSpan{ _body_buffer.data, 0 };
        _header_length = 0;
        _version = HttpVersion::V11;
    }

    KiottyNetHttpStatus HttpRequestStore::takeRequestLine(Span<const char> line)
    {
        const Span<char> copied = claimHeaderRoom(line);
        if (copied.length != line.length) { return TOO_LARGE; }
        const Span<const char> text{ copied.data, copied.length };
        const RequestLineResult parsed = parseRequestLine(text);
        if (!parsed) { return parsed.code; }
        _request.method = parsed.value.method;
        _request.path = spanOf(parsed.value.path);
        _request.query = spanOf(parsed.value.query);
        _version = parsed.value.version;
        return NO_ERROR_STATUS;
    }

    KiottyNetHttpStatus HttpRequestStore::takeHeader(Span<const char> line)
    {
        const bool is_full = _request.header_count >= _headers.length;
        if (is_full) { return TOO_LARGE; }
        const size_t room = _header_buffer.length - _header_length;
        const Span<char> copied = claimHeaderRoom(line);
        if (copied.length != line.length) { return TOO_LARGE; }
        const Span<const char> text{ copied.data, copied.length };
        const HeaderResult parsed = parseHeaderLine(text, room);
        if (!parsed) { return parsed.code; }
        _headers[_request.header_count] = parsed.value;
        _request.header_count += 1;
        return NO_ERROR_STATUS;
    }

    FramingResult HttpRequestStore::finishHeaders()
    {
        const Span<const KiottyNetHttpHeader> found{ _headers.data, _request.header_count };
        const Persistence persistence = decidePersistence(_version, found);
        _request.keep_alive = persistence == Persistence::KEEP_ALIVE ? 1 : 0;
        return selectBodyFraming(found);
    }

    KiottyNetHttpStatus HttpRequestStore::appendBody(Span<const char> data)
    {
        const size_t used = _request.body.length;
        const size_t room = _body_buffer.length - used;
        if (data.length > room) { return CONTENT_TOO_LARGE; }
        const Span<char> target = _body_buffer.skip(used);
        (void)target.copyFrom(data);
        _request.body.length = used + data.length;
        return NO_ERROR_STATUS;
    }

    bool HttpRequestStore::expectsContinue() const
    {
        const Span<const KiottyNetHttpHeader> found{ _headers.data, _request.header_count };
        return implementation::expectsContinue(_version, found);
    }

    const KiottyNetHttpRequest& HttpRequestStore::request() const
    {
        return _request;
    }

    Span<char> HttpRequestStore::claimHeaderRoom(Span<const char> line)
    {
        const size_t room = _header_buffer.length - _header_length;
        if (line.length > room) { return Span<char>{ nullptr, 0 }; }
        const Span<char> target{ _header_buffer.data + _header_length, line.length };
        (void)target.copyFrom(line);
        _header_length += line.length;
        return target;
    }

    namespace
    {
        KiottyNetSpan spanOf(Span<const char> text)
        {
            return KiottyNetSpan{ text.data, text.length };
        }
    }
}
