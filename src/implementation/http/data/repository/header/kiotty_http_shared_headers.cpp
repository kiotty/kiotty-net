#include "http/data/repository/header/kiotty_http_shared_headers.h"

#include "core/kiotty_checked.h"
#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/request/kiotty_http_request_check_rules.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char WIPED = '\0';

        size_t bytesOf(const KiottyNetHttpHeader& header);
        size_t offsetOf(Span<const KiottyNetHttpHeader> entries, size_t index);
        void   shiftTextDown(Span<char> text, size_t from, size_t to);
        void   relinkEntries(Span<KiottyNetHttpHeader> entries, Span<char> text);
    }

    HttpSharedHeaders::HttpSharedHeaders(Span<char> text, Span<KiottyNetHttpHeader> entries)
        : _text(text)
        , _entries(entries)
        , _text_length(0)
        , _count(0)
    {
    }

    KiottyNetCode HttpSharedHeaders::set(Span<const char> name, Span<const char> value)
    {
        const KiottyNetSpan name_span{ name.data, name.length };
        const KiottyNetSpan value_span{ value.data, value.length };
        const KiottyNetHttpHeader header{ name_span, value_span };
        const KiottyNetCode checked = checkRequestHeader(header);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return checked; }
        const Span<const char> trimmed = trimWhitespace(value);
        const Result<size_t, KiottyNetCode> bytes = checkedAdd(name.length, trimmed.length);
        if (!bytes) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const size_t index = findIndex(name);
        const KiottyNetCode room = checkRoom(index, bytes.value);
        if (room != KIOTTY_NET_CODE_SUCCESS) { return room; }
        if (index < _count) { removeAt(index); }
        append(name, trimmed);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode HttpSharedHeaders::remove(Span<const char> name)
    {
        const size_t index = findIndex(name);
        if (index < _count) { removeAt(index); }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void HttpSharedHeaders::clear()
    {
        const Span<char> used{ _text.data, _text_length };
        used.fill(WIPED);
        _text_length = 0;
        _count = 0;
    }

    Span<const KiottyNetHttpHeader> HttpSharedHeaders::list() const
    {
        return Span<const KiottyNetHttpHeader>{ _entries.data, _count };
    }

    size_t HttpSharedHeaders::findIndex(Span<const char> name) const
    {
        for (size_t i = 0; i < _count; ++i)
        {
            if (equalsIgnoreCase(spanOf(_entries[i].name), name)) { return i; }
        }
        return _count;
    }

    KiottyNetCode HttpSharedHeaders::checkRoom(size_t index, size_t bytes) const
    {
        const bool is_new = index >= _count;
        const bool has_slot = _count < _entries.length;
        if (is_new && !has_slot) { return KIOTTY_NET_CODE_BUFFER_FULL; }
        const size_t freed = is_new ? 0 : bytesOf(_entries[index]);
        const size_t kept = _text_length - freed;
        const bool is_fitting = bytes <= _text.length - kept;
        return is_fitting ? KIOTTY_NET_CODE_SUCCESS : KIOTTY_NET_CODE_BUFFER_FULL;
    }

    void HttpSharedHeaders::removeAt(size_t index)
    {
        const size_t start = offsetOf(list(), index);
        const size_t bytes = bytesOf(_entries[index]);
        shiftTextDown(Span<char>{ _text.data, _text_length }, start + bytes, start);
        _text_length -= bytes;
        const Span<char> freed{ _text.data + _text_length, bytes };
        freed.fill(WIPED);
        for (size_t i = index + 1; i < _count; ++i)
        {
            _entries[i - 1] = _entries[i];
        }
        _count -= 1;
        relinkEntries(Span<KiottyNetHttpHeader>{ _entries.data, _count }, _text);
    }

    void HttpSharedHeaders::append(Span<const char> name, Span<const char> value)
    {
        const Span<char> target = _text.skip(_text_length);
        (void)target.copyFrom(name);
        (void)target.skip(name.length).copyFrom(value);
        const KiottyNetSpan stored_name{ target.data, name.length };
        const KiottyNetSpan stored_value{ target.data + name.length, value.length };
        _entries[_count] = KiottyNetHttpHeader{ stored_name, stored_value };
        _count += 1;
        _text_length += name.length + value.length;
    }

    namespace
    {
        size_t bytesOf(const KiottyNetHttpHeader& header)
        {
            return header.name.length + header.value.length;
        }

        size_t offsetOf(Span<const KiottyNetHttpHeader> entries, size_t index)
        {
            size_t offset = 0;
            for (size_t i = 0; i < index; ++i)
            {
                offset += bytesOf(entries[i]);
            }
            return offset;
        }

        void shiftTextDown(Span<char> text, size_t from, size_t to)
        {
            for (size_t i = from; i < text.length; ++i)
            {
                text[to + i - from] = text[i];
            }
        }

        void relinkEntries(Span<KiottyNetHttpHeader> entries, Span<char> text)
        {
            size_t offset = 0;
            for (KiottyNetHttpHeader& entry : entries)
            {
                entry.name.data = text.data + offset;
                offset += entry.name.length;
                entry.value.data = text.data + offset;
                offset += entry.value.length;
            }
        }
    }
}
