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
        constexpr char    LAST_CHUNK[] = "0\r\n\r\n";

        constexpr const char* RESERVED_NAMES[] = {
            "Content-Length", "Transfer-Encoding", "Connection", "Date", "Content-Type",
        };

        ResponseLayout   emptyLayout(Span<char> buffer);
        Span<const char> literalOf(const char* text);
        bool             isReservedName(Span<const char> name);
        bool             isFieldValue(Span<const char> value);
        bool             isBodiless(int32_t status);
        bool             isNamedType(KiottyNetHttpContentType type, BodyStorage storage);
        void             writeField(Span<char> target,
                                    Span<const char> name,
                                    Span<const char> value);
        void             appendFraming(PrefixText& prefix, const ResponseLayout& layout);
        Span<const char> sealedBodyOf(const ResponseLayout& layout, bool is_bodiless);
        size_t           smallerOf(size_t left, size_t right);
    }

    ResponseFrame::ResponseFrame(const ResponseLayout& layout)
        : _layout(layout)
    {
    }

    Span<const char> ResponseFrame::chunkAt(size_t offset, size_t max_length) const
    {
        const size_t header_length = _layout.header_end - _layout.header_start;
        const bool is_header = offset < header_length;
        const size_t start = is_header ? _layout.header_start + offset : 0;
        const size_t header_rest = is_header ? header_length - offset : 0;
        const Span<const char> header{ _layout.buffer.data + start, header_rest };
        const Span<const char> body{ _layout.body.data, _layout.sendable_body };
        const Span<const char> rest = is_header ? header : body.skip(offset - header_length);
        return Span<const char>{ rest.data, smallerOf(rest.length, max_length) };
    }

    KiottyNetCode ResponseFrame::checkChunk(Span<const char> data) const
    {
        const bool has_data = data.data != nullptr && data.length > 0;
        if (!isStream() || !has_data) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const bool is_text = textKindOf(_layout.type) != TextKind::BINARY;
        if (is_text && !isUtf8(data)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool ResponseFrame::isStream() const
    {
        return _layout.has_body && _layout.storage == BodyStorage::STREAM;
    }

    bool ResponseFrame::hasSendableBody() const
    {
        return _layout.sendable_body > 0;
    }

    Span<const char> ResponseFrame::frameChunk(Span<const char> data,
                                               size_t kept,
                                               size_t limit) const
    {
        const Span<const char> none{ nullptr, 0 };
        const Span<char> area = _layout.buffer.skip(_layout.header_start);
        const size_t room = smallerOf(area.length, limit);
        const ChunkHead head = chunkHeadOf(data.length);
        const size_t framing = kept + head.length() + LINE_END_BYTES;
        const bool has_room = framing <= room && data.length <= room - framing;
        if (!has_room) { return none; }
        const Span<char> head_target = area.skip(kept);
        (void)head_target.copyFrom(head.span());
        const Span<char> data_target = head_target.skip(head.length());
        (void)data_target.copyFrom(data);
        const Span<char> tail_target = data_target.skip(data.length);
        (void)tail_target.copyFrom(literalOf("\r\n"));
        return Span<const char>{ area.data, framing + data.length };
    }

    HttpResponseStore::HttpResponseStore(Span<char> buffer)
        : _layout(emptyLayout(buffer))
        , _custom()
        , _status(0)
        , _is_touched(false)
    {
    }

    void HttpResponseStore::begin()
    {
        _layout = emptyLayout(_layout.buffer);
        _custom.clear();
        _status = 0;
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
        const Span<char> target{ _layout.buffer.data + _layout.header_end, needed };
        writeField(target, name, value);
        _layout.header_end += needed;
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
        const KiottyNetCode checked = checkBodyType(type, storage);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const bool is_stream = storage == BodyStorage::STREAM;
        const Span<const char> body{ data.data, is_stream ? 0 : data.length };
        const bool is_text = textKindOf(type) != TextKind::BINARY;
        if (is_text && !isUtf8(body)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
        const bool is_copy = storage == BodyStorage::COPY;
        const size_t room = _layout.buffer.length - _layout.header_end - LINE_END_BYTES;
        if (is_copy && body.length > room) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const size_t end = _layout.buffer.length;
        _layout.body_start = is_copy ? end - body.length : end;
        const Span<char> target{ _layout.buffer.data + _layout.body_start, body.length };
        if (is_copy) { (void)target.copyFrom(body); }
        _layout.body = is_copy ? Span<const char>{ target.data, target.length } : body;
        _layout.type = type;
        _layout.storage = storage;
        _layout.has_body = true;
        _is_touched = true;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpResponseStore::seal(const ResponseSeal& seal)
    {
        const int32_t status = finalStatus();
        const bool is_bodiless = isBodiless(status);
        const bool is_head = seal.method == KIOTTY_NET_HTTP_METHOD_HEAD;
        _layout.body = sealedBodyOf(_layout, is_bodiless);
        PrefixText prefix;
        (void)appendStatusLine(prefix, status);
        (void)appendDateField(prefix, currentUtcTime());
        if (!is_bodiless) { appendFraming(prefix, _layout); }
        const bool has_type = _layout.has_body && !is_bodiless;
        if (has_type) { (void)appendField(prefix, literalOf("Content-Type"), contentTypeText()); }
        const bool is_closing = seal.persistence == Persistence::CLOSE;
        if (is_closing) { (void)appendField(prefix, literalOf("Connection"), literalOf("close")); }
        _layout.header_start = RESPONSE_PREFIX_BYTES - prefix.length();
        const Span<char> head{ _layout.buffer.data + _layout.header_start, prefix.length() };
        (void)head.copyFrom(prefix.span());
        const Span<char> end{ _layout.buffer.data + _layout.header_end, LINE_END_BYTES };
        (void)end.copyFrom(literalOf("\r\n"));
        _layout.header_end += LINE_END_BYTES;
        _layout.sendable_body = is_head || is_bodiless ? 0 : _layout.body.length;
    }

    ResponseFrame HttpResponseStore::frame() const
    {
        return ResponseFrame(_layout);
    }

    KiottyNetCode HttpResponseStore::checkBodyType(KiottyNetHttpContentType type,
                                                   BodyStorage storage) const
    {
        if (isNamedType(type, storage)) { return KIOTTY_NET_CODE_SUCCESS; }
        const bool is_custom = type == KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM && !_custom.empty();
        if (is_custom) { return KIOTTY_NET_CODE_SUCCESS; }
        return KIOTTY_NET_CODE_INVALID_ARGUMENT;
    }

    size_t HttpResponseStore::headerRoom() const
    {
        return _layout.body_start - _layout.header_end - LINE_END_BYTES;
    }

    Span<const char> HttpResponseStore::contentTypeText() const
    {
        if (_layout.type == KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM) { return _custom.span(); }
        return contentTypeTextOf(_layout.type);
    }

    int32_t HttpResponseStore::finalStatus() const
    {
        if (_status != 0) { return _status; }
        if (_is_touched) { return STATUS_OK; }
        return STATUS_NOT_FOUND;
    }

    namespace
    {
        ResponseLayout emptyLayout(Span<char> buffer)
        {
            return ResponseLayout{
                buffer,
                Span<const char>{ nullptr, 0 },
                RESPONSE_PREFIX_BYTES,
                RESPONSE_PREFIX_BYTES,
                buffer.length,
                0,
                KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM,
                BodyStorage::COPY,
                false,
            };
        }

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

        bool isNamedType(KiottyNetHttpContentType type, BodyStorage storage)
        {
            const bool is_listed = type >= KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM
                                   && type < KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
            const bool is_event_stream = type == KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM;
            const bool is_stream = storage == BodyStorage::STREAM;
            return is_listed && (is_stream || !is_event_stream);
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

        void appendFraming(PrefixText& prefix, const ResponseLayout& layout)
        {
            const bool is_stream = layout.has_body && layout.storage == BodyStorage::STREAM;
            const Span<const char> name = literalOf("Transfer-Encoding");
            if (is_stream) { (void)appendField(prefix, name, literalOf("chunked")); }
            if (!is_stream) { (void)appendLengthField(prefix, layout.body.length); }
        }

        Span<const char> sealedBodyOf(const ResponseLayout& layout, bool is_bodiless)
        {
            const bool is_stream = layout.has_body && layout.storage == BodyStorage::STREAM;
            if (!is_stream) { return layout.body; }
            if (is_bodiless) { return Span<const char>{ nullptr, 0 }; }
            return Span<const char>{ LAST_CHUNK, sizeof(LAST_CHUNK) - 1 };
        }

        size_t smallerOf(size_t left, size_t right)
        {
            return left < right ? left : right;
        }
    }
}
