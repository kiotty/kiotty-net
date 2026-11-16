#include "http/domain/entity/kiotty_http_request_check.h"

#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/domain/entity/kiotty_http_content_type.h"
#include "http/domain/entity/kiotty_http_header_line.h"
#include "http/domain/entity/kiotty_http_token.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char          WHOLE_SERVER[] = "*";
        constexpr const char*   RESERVED_REQUEST_NAMES[] = {
            "Host",
            "Content-Length",
            "Transfer-Encoding",
            "Connection",
        };

        bool isReadable(KiottyNetSpan span);
        bool isReservedRequestName(Span<const char> name);
        bool isNamedType(KiottyNetHttpContentType type);
    }

    KiottyNetCode checkRequestHeader(const KiottyNetHttpHeader& header)
    {
        const bool is_readable = isReadable(header.name) && isReadable(header.value);
        if (!is_readable) { return INVALID; }
        const Span<const char> name = spanOf(header.name);
        if (!isToken(name)) { return INVALID; }
        if (isReservedRequestName(name)) { return INVALID; }
        if (!isFieldValue(spanOf(header.value))) { return INVALID; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode checkRequestPath(KiottyNetHttpMethod method, Span<const char> path)
    {
        const bool is_options = method == KIOTTY_NET_HTTP_METHOD_OPTIONS;
        const bool is_whole_server = is_options && equalsLiteral(path, WHOLE_SERVER);
        if (is_whole_server) { return KIOTTY_NET_CODE_SUCCESS; }
        const bool is_origin_form = path.length > 0 && path[0] == '/';
        if (!is_origin_form) { return INVALID; }
        for (size_t i = 0; i < path.length; ++i)
        {
            if (!isVisibleCharacter(path[i])) { return INVALID; }
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode checkRequestBody(KiottyNetHttpContentType type, Span<const char> body)
    {
        const bool is_custom = type == KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM;
        const bool is_known = is_custom || isNamedType(type);
        if (!is_known) { return INVALID; }
        const bool is_readable = body.data != nullptr || body.length == 0;
        if (!is_readable) { return INVALID; }
        const bool is_text = textKindOf(type) != TextKind::BINARY;
        if (!is_text) { return KIOTTY_NET_CODE_SUCCESS; }
        if (!isUtf8(body)) { return KIOTTY_NET_CODE_INVALID_ENCODING; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    namespace
    {
        bool isReadable(KiottyNetSpan span)
        {
            return span.data != nullptr || span.length == 0;
        }

        bool isReservedRequestName(Span<const char> name)
        {
            for (const char* reserved : RESERVED_REQUEST_NAMES)
            {
                if (equalsLiteralIgnoreCase(name, reserved)) { return true; }
            }
            return false;
        }

        bool isNamedType(KiottyNetHttpContentType type)
        {
            const int32_t index = static_cast<int32_t>(type);
            return index >= 0 && index < KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
        }
    }
}
