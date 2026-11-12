#include "data/repository/http/kiotty_http_response_store.h"

#include "core/kiotty_text.h"
#include "data/infra/clock/kiotty_wall_clock.h"
#include "domain/usecases/http/kiotty_http_content_type.h"
#include "domain/usecases/http/kiotty_http_response_text.h"
#include "domain/usecases/http/kiotty_http_token.h"

#include <string>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t STATUS_MIN = 100;
        constexpr int32_t STATUS_MAX = 599;
        constexpr int32_t STATUS_OK = 200;
        constexpr int32_t STATUS_NOT_FOUND = 404;
        constexpr int32_t STATUS_NO_CONTENT = 204;
        constexpr int32_t STATUS_NOT_MODIFIED = 304;
        constexpr size_t  LINE_END_BYTES = 2;
        constexpr size_t  SEPARATOR_BYTES = 2;

        constexpr const char* RESERVED_NAMES[] = {
            "Content-Length", "Transfer-Encoding", "Connection", "Date", "Content-Type",
        };

        Span<const char> literalOf(const char* text);
        bool             isReservedName(Span<const char> name);
        bool             isFieldValue(Span<const char> value);
        bool             isBodiless(int32_t status);
        void             writeField(Span<char> target,
                                    Span<const char> name,
                                    Span<const char> value);
    }

    HttpResponseStore::HttpResponseStore(Span<char> buffer)
        : _buffer(buffer)
        , _body{ nullptr, 0 }
        , _custom()
        , _header_start(RESPONSE_PREFIX_BYTES)
        , _header_end(RESPONSE_PREFIX_BYTES)
        , _body_start(buffer.length)
        , _sendable_body(0)
        , _status(0)
        , _type(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM)
        , _has_body(false)
        , _is_touched(false)
    {
    }

    void HttpResponseStore::begin()
    {
        _body = Span<const char>{ nullptr, 0 };
        _custom.clear();
        _header_start = RESPONSE_PREFIX_BYTES;
        _header_end = RESPONSE_PREFIX_BYTES;
        _body_start = _buffer.length;
        _sendable_body = 0;
        _status = 0;
        _type = KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM;
        _has_body = false;
        _is_touched = false;
    }

    KiottyNetCode HttpResponseStore::setStatus(int32_t status)
    {
        const bool is_valid = status >= STATUS_MIN && status <= STATUS_MAX;
        if (!is_valid) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _status = status;
        _is_touched = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpResponseStore::addHeader(Span<const char> name, Span<const char> value)
    {
        const bool is_name = isToken(name) && !isReservedName(name);
        if (!is_name) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isFieldValue(value)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isUtf8(value)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
        const size_t needed = name.length + value.length + SEPARATOR_BYTES + LINE_END_BYTES;
        if (needed > headerRoom()) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const Span<char> target{ _buffer.data + _header_end, needed };
        writeField(target, name, value);
        _header_end += needed;
        _is_touched = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpResponseStore::setCustomContentType(const char* content_type)
    {
        const TextResult parsed = parseCustomContentType(content_type);
        if (!parsed) { return parsed.code; }
        (void)_custom.assign(parsed.value);
        _is_touched = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpResponseStore::setBody(KiottyNetHttpContentType type,
                                             KiottyNetSpan data,
                                             BodyStorage storage)
    {
        const bool is_missing = data.data == nullptr && data.length > 0;
        if (is_missing) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode checked = checkBodyType(type);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const Span<const char> body{ data.data, data.length };
        const bool is_text = textKindOf(type) != TextKind::BINARY;
        if (is_text && !isUtf8(body)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
        const bool is_copy = storage == BodyStorage::COPY;
        const size_t room = _buffer.length - _header_end - LINE_END_BYTES;
        if (is_copy && body.length > room) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        _body_start = is_copy ? _buffer.length - body.length : _buffer.length;
        const Span<char> target{ _buffer.data + _body_start, body.length };
        if (is_copy) { (void)target.copyFrom(body); }
        _body = is_copy ? Span<const char>{ target.data, target.length } : body;
        _type = type;
        _has_body = true;
        _is_touched = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpResponseStore::seal(const ResponseSeal& seal)
    {
        const int32_t status = finalStatus();
        const bool is_bodiless = isBodiless(status);
        const bool is_head = seal.method == KIOTTY_NET_HTTP_METHOD_HEAD;
        PrefixText prefix;
        (void)appendStatusLine(prefix, status);
        (void)appendDateField(prefix, currentUtcTime());
        if (!is_bodiless) { (void)appendLengthField(prefix, _body.length); }
        const bool has_type = _has_body && !is_bodiless;
        if (has_type) { (void)appendField(prefix, literalOf("Content-Type"), contentTypeText()); }
        const bool is_closing = seal.persistence == Persistence::CLOSE;
        if (is_closing) { (void)appendField(prefix, literalOf("Connection"), literalOf("close")); }
        _header_start = RESPONSE_PREFIX_BYTES - prefix.length();
        const Span<char> head{ _buffer.data + _header_start, prefix.length() };
        (void)head.copyFrom(prefix.span());
        const Span<char> end{ _buffer.data + _header_end, LINE_END_BYTES };
        (void)end.copyFrom(literalOf("\r\n"));
        _header_end += LINE_END_BYTES;
        _sendable_body = is_head || is_bodiless ? 0 : _body.length;
    }

    Span<const char> HttpResponseStore::chunkAt(size_t offset, size_t max_length) const
    {
        const size_t header_length = _header_end - _header_start;
        const bool is_header = offset < header_length;
        const size_t start = is_header ? _header_start + offset : 0;
        const size_t header_rest = is_header ? header_length - offset : 0;
        const Span<const char> header{ _buffer.data + start, header_rest };
        const Span<const char> body{ _body.data, _sendable_body };
        const Span<const char> rest = is_header ? header : body.skip(offset - header_length);
        const size_t length = rest.length < max_length ? rest.length : max_length;
        return Span<const char>{ rest.data, length };
    }

    KiottyNetCode HttpResponseStore::checkBodyType(KiottyNetHttpContentType type) const
    {
        const bool is_named = type >= KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM
                              && type < KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION
                              && type != KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM;
        if (is_named) { return KIOTTY_NET_CODE_SUCCESS; }
        const bool is_custom = type == KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM && !_custom.empty();
        if (is_custom) { return KIOTTY_NET_CODE_SUCCESS; }
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    size_t HttpResponseStore::headerRoom() const
    {
        return _body_start - _header_end - LINE_END_BYTES;
    }

    Span<const char> HttpResponseStore::contentTypeText() const
    {
        if (_type == KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM) { return _custom.span(); }
        return contentTypeTextOf(_type);
    }

    int32_t HttpResponseStore::finalStatus() const
    {
        if (_status != 0) { return _status; }
        if (_is_touched) { return STATUS_OK; }
        return STATUS_NOT_FOUND;
    }

    namespace
    {
        Span<const char> literalOf(const char* text)
        {
            return Span<const char>{ text, std::char_traits<char>::length(text) };
        }

        bool isReservedName(Span<const char> name)
        {
            for (const char* reserved : RESERVED_NAMES)
            {
                if (equalsLiteralIgnoreCase(name, reserved)) { return true; }
            }
            return false;
        }

        bool isFieldValue(Span<const char> value)
        {
            for (size_t i = 0; i < value.length; ++i)
            {
                const unsigned char byte = static_cast<unsigned char>(value[i]);
                const bool is_control = byte < ' ' && value[i] != '\t';
                const bool is_delete = value[i] == '\x7F';
                if (is_control || is_delete) { return false; }
            }
            return true;
        }

        bool isBodiless(int32_t status)
        {
            const bool is_informational = status < STATUS_OK;
            return is_informational || status == STATUS_NO_CONTENT || status == STATUS_NOT_MODIFIED;
        }

        void writeField(Span<char> target, Span<const char> name, Span<const char> value)
        {
            (void)target.copyFrom(name);
            const Span<char> separator = target.skip(name.length);
            (void)separator.copyFrom(literalOf(": "));
            const Span<char> text = separator.skip(SEPARATOR_BYTES);
            (void)text.copyFrom(value);
            const Span<char> end = text.skip(value.length);
            (void)end.copyFrom(literalOf("\r\n"));
        }
    }
}
