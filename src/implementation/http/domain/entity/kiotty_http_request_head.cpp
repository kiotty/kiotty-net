#include "http/domain/entity/kiotty_http_request_head.h"

#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/domain/entity/kiotty_http_constants.h"
#include "http/domain/entity/kiotty_http_content_type.h"
#include "http/domain/entity/kiotty_http_request_check.h"
#include "http/domain/entity/kiotty_http_request_line.h"
#include "http/domain/entity/kiotty_http_response_text.h"
#include "http/domain/entity/kiotty_http_token.h"
#include "http/domain/entity/kiotty_http_upload.h"

namespace kiotty::net::implementation
{
    namespace
    {
        struct HeadCursor
        {
            Span<char> buffer;
            size_t     length;
        };

        struct HeadPlan
        {
            size_t line_count;
            bool   has_content_type;
            bool   has_content_length;
            bool   is_chunked;
        };

        constexpr size_t        HOST_LINE_COUNT = 1;
        constexpr char          OPEN_BRACKET[] = "[";
        constexpr char          CLOSE_BRACKET[] = "]";
        constexpr char          PORT_SEPARATOR[] = ":";
        constexpr char          FIELD_SEPARATOR[] = ": ";
        constexpr char          SPACE[] = " ";
        constexpr char          VERSION_LINE_END[] = " HTTP/1.1\r\n";
        constexpr char          HOST[] = "Host";
        constexpr char          PIPE_HOST[] = "localhost";
        constexpr char          CONTENT_TYPE[] = "Content-Type";
        constexpr char          CHUNKED_CODING[] = "chunked";

        bool          isHostText(Span<const char> host);
        bool          isLiteralV6(Span<const char> host);
        KiottyNetCode checkHeadParts(const RequestHead& head);
        HeadPlan      planHead(const RequestHead& head);
        size_t        countSharedSent(const RequestHead& head, const HeadPlan& plan);
        bool isSharedSent(const KiottyNetHttpHeader& header,
                          const RequestHead& head,
                          const HeadPlan& plan);
        bool hasHeaderNamed(Span<const KiottyNetHttpHeader> headers, Span<const char> name);
        bool needsLength(KiottyNetHttpMethod method);
        bool writeStartLines(HeadCursor& cursor, const RequestHead& head);
        bool writeSharedHeaders(HeadCursor& cursor, const RequestHead& head, const HeadPlan& plan);
        bool writeExtraHeaders(HeadCursor& cursor, Span<const KiottyNetHttpHeader> headers);
        bool writeBodyFields(HeadCursor& cursor, const RequestHead& head, const HeadPlan& plan);
        bool putField(HeadCursor& cursor, Span<const char> name, Span<const char> value);
        bool appendText(HeadCursor& cursor, Span<const char> text);
    }

    HostFieldResult buildHostField(Span<const char> host, uint16_t port, uint16_t default_port)
    {
        if (!isHostText(host)) { return HostFieldResult::failure(INVALID); }
        const bool is_literal_v6 = isLiteralV6(host);
        const bool has_port = port != default_port;
        const DecimalText digits = decimalOf(port);
        HostField field;
        bool is_written = !is_literal_v6 || field.append(textOf(OPEN_BRACKET));
        is_written = is_written && field.append(host);
        is_written = is_written && (!is_literal_v6 || field.append(textOf(CLOSE_BRACKET)));
        is_written = is_written && (!has_port || field.append(textOf(PORT_SEPARATOR)));
        is_written = is_written && (!has_port || field.append(digits.span()));
        if (!is_written) { return HostFieldResult::failure(INVALID); }
        return HostFieldResult::success(field);
    }

    HostFieldResult hostFieldOf(const KiottyNetAddress& address, uint16_t default_port)
    {
        const bool is_pipe = address.transport == KIOTTY_NET_TRANSPORT_PIPE;
        if (is_pipe) { return buildHostField(textOf(PIPE_HOST), default_port, default_port); }
        return buildHostField(textOf(address.host), address.port, default_port);
    }

    HeadLengthResult writeRequestHead(Span<char> buffer, const RequestHead& head)
    {
        const KiottyNetCode checked = checkHeadParts(head);
        if (checked != KIOTTY_NET_CODE_SUCCESS) { return HeadLengthResult::failure(checked); }
        const HeadPlan plan = planHead(head);
        const bool is_table_full = plan.line_count > head.max_headers;
        if (is_table_full) { return HeadLengthResult::failure(KIOTTY_NET_CODE_TABLE_FULL); }
        HeadCursor cursor{ buffer, 0 };
        bool is_written = writeStartLines(cursor, head);
        is_written = is_written && writeSharedHeaders(cursor, head, plan);
        is_written = is_written && writeExtraHeaders(cursor, head.extra_headers);
        is_written = is_written && writeBodyFields(cursor, head, plan);
        is_written = is_written && appendText(cursor, textOf(LINE_END));
        if (!is_written) { return HeadLengthResult::failure(KIOTTY_NET_CODE_REQUEST_TOO_LARGE); }
        return HeadLengthResult::success(cursor.length);
    }

    namespace
    {
        bool isHostText(Span<const char> host)
        {
            if (host.empty()) { return false; }
            for (size_t i = 0; i < host.length; ++i)
            {
                if (!isVisibleCharacter(host[i])) { return false; }
            }
            return true;
        }

        bool isLiteralV6(Span<const char> host)
        {
            const bool has_colon = findCharacter(host, ':', 0) < host.length;
            const bool is_bracketed = host[0] == OPEN_BRACKET[0];
            return has_colon && !is_bracketed;
        }

        KiottyNetCode checkHeadParts(const RequestHead& head)
        {
            const bool is_method = methodTextOf(head.method).length > 0;
            if (!is_method) { return INVALID; }
            const KiottyNetCode path = checkRequestPath(head.method, head.path);
            if (path != KIOTTY_NET_CODE_SUCCESS) { return path; }
            const bool is_table_full = head.extra_headers.length > head.max_headers;
            if (is_table_full) { return KIOTTY_NET_CODE_TABLE_FULL; }
            for (const KiottyNetHttpHeader& header : head.extra_headers)
            {
                const KiottyNetCode code = checkRequestHeader(header);
                if (code != KIOTTY_NET_CODE_SUCCESS) { return code; }
            }
            return KIOTTY_NET_CODE_SUCCESS;
        }

        HeadPlan planHead(const RequestHead& head)
        {
            const bool is_chunked = head.body_length == CHUNKED_BODY_LENGTH;
            const bool has_body = head.body_length > 0;
            const bool is_custom = head.type == KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM;
            const bool has_own_type = hasHeaderNamed(head.extra_headers, textOf(CONTENT_TYPE));
            const bool has_content_type = has_body && !is_custom && !has_own_type;
            const bool is_counted = has_body || needsLength(head.method);
            const bool has_content_length = is_counted && !is_chunked;
            HeadPlan plan{ 0, has_content_type, has_content_length, is_chunked };
            plan.line_count = HOST_LINE_COUNT + countSharedSent(head, plan);
            plan.line_count += head.extra_headers.length;
            plan.line_count += has_content_type ? 1 : 0;
            plan.line_count += has_content_length || is_chunked ? 1 : 0;
            return plan;
        }

        size_t countSharedSent(const RequestHead& head, const HeadPlan& plan)
        {
            size_t count = 0;
            for (const KiottyNetHttpHeader& header : head.shared_headers)
            {
                count += isSharedSent(header, head, plan) ? 1 : 0;
            }
            return count;
        }

        bool isSharedSent(const KiottyNetHttpHeader& header,
                          const RequestHead& head,
                          const HeadPlan& plan)
        {
            const Span<const char> name = spanOf(header.name);
            if (hasHeaderNamed(head.extra_headers, name)) { return false; }
            const bool is_type = equalsLiteralIgnoreCase(name, CONTENT_TYPE);
            return !(is_type && plan.has_content_type);
        }

        bool hasHeaderNamed(Span<const KiottyNetHttpHeader> headers, Span<const char> name)
        {
            for (const KiottyNetHttpHeader& header : headers)
            {
                if (equalsIgnoreCase(spanOf(header.name), name)) { return true; }
            }
            return false;
        }

        bool needsLength(KiottyNetHttpMethod method)
        {
            const bool is_post = method == KIOTTY_NET_HTTP_METHOD_POST;
            const bool is_put = method == KIOTTY_NET_HTTP_METHOD_PUT;
            return is_post || is_put || method == KIOTTY_NET_HTTP_METHOD_PATCH;
        }

        bool writeStartLines(HeadCursor& cursor, const RequestHead& head)
        {
            bool is_written = appendText(cursor, methodTextOf(head.method));
            is_written = is_written && appendText(cursor, textOf(SPACE));
            is_written = is_written && appendText(cursor, head.path);
            is_written = is_written && appendText(cursor, textOf(VERSION_LINE_END));
            return is_written && putField(cursor, textOf(HOST), head.host_field);
        }

        bool writeSharedHeaders(HeadCursor& cursor, const RequestHead& head, const HeadPlan& plan)
        {
            for (const KiottyNetHttpHeader& header : head.shared_headers)
            {
                const bool is_sent = isSharedSent(header, head, plan);
                const Span<const char> name = spanOf(header.name);
                const bool is_written = !is_sent || putField(cursor, name, spanOf(header.value));
                if (!is_written) { return false; }
            }
            return true;
        }

        bool writeExtraHeaders(HeadCursor& cursor, Span<const KiottyNetHttpHeader> headers)
        {
            for (const KiottyNetHttpHeader& header : headers)
            {
                const Span<const char> value = trimWhitespace(spanOf(header.value));
                const bool is_written = putField(cursor, spanOf(header.name), value);
                if (!is_written) { return false; }
            }
            return true;
        }

        bool writeBodyFields(HeadCursor& cursor, const RequestHead& head, const HeadPlan& plan)
        {
            const Span<const char> type = contentTypeTextOf(head.type);
            const DecimalText length = decimalOf(head.body_length);
            const bool is_typed = !plan.has_content_type
                                  || putField(cursor, textOf(CONTENT_TYPE), type);
            if (!is_typed) { return false; }
            if (plan.is_chunked)
            {
                return putField(cursor, textOf(TRANSFER_ENCODING), textOf(CHUNKED_CODING));
            }
            return !plan.has_content_length
                   || putField(cursor, textOf(CONTENT_LENGTH), length.span());
        }

        bool putField(HeadCursor& cursor, Span<const char> name, Span<const char> value)
        {
            bool is_written = appendText(cursor, name);
            is_written = is_written && appendText(cursor, textOf(FIELD_SEPARATOR));
            is_written = is_written && appendText(cursor, value);
            return is_written && appendText(cursor, textOf(LINE_END));
        }

        bool appendText(HeadCursor& cursor, Span<const char> text)
        {
            const Span<char> room = cursor.buffer.skip(cursor.length);
            if (!room.copyFrom(text)) { return false; }
            cursor.length += text.length;
            return true;
        }
    }
}
