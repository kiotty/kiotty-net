#include "http/domain/entity/kiotty_http_reply_headers.h"

#include "core/kiotty_text.h"
#include "http/domain/entity/kiotty_http_request_line.h"
#include "http/domain/entity/kiotty_http_token.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char          ALLOW[] = "Allow";
        constexpr char          LIST_SEPARATOR = ',';
        constexpr KiottyNetSpan NO_HEADER{ nullptr, 0 };

        uint32_t methodsOfList(Span<const char> list);
        uint32_t methodBitOf(Span<const char> token);
    }

    KiottyNetSpan findReplyHeader(Span<const KiottyNetHttpHeader> headers, Span<const char> name)
    {
        for (const KiottyNetHttpHeader& header : headers)
        {
            if (equalsIgnoreCase(spanOf(header.name), name)) { return header.value; }
        }
        return NO_HEADER;
    }

    uint32_t allowedMethodsOf(Span<const KiottyNetHttpHeader> headers)
    {
        uint32_t methods = 0;
        for (const KiottyNetHttpHeader& header : headers)
        {
            const bool is_allow = equalsLiteralIgnoreCase(spanOf(header.name), ALLOW);
            methods |= is_allow ? methodsOfList(spanOf(header.value)) : 0U;
        }
        return methods;
    }

    namespace
    {
        uint32_t methodsOfList(Span<const char> list)
        {
            uint32_t methods = 0;
            size_t start = 0;
            while (start <= list.length)
            {
                const size_t comma = findCharacter(list, LIST_SEPARATOR, start);
                const Span<const char> item{ list.data + start, comma - start };
                methods |= methodBitOf(trimWhitespace(item));
                start = comma + 1;
            }
            return methods;
        }

        uint32_t methodBitOf(Span<const char> token)
        {
            for (int32_t index = 0; index < KIOTTY_NET_HTTP_METHOD_COUNT; ++index)
            {
                const KiottyNetHttpMethod method = static_cast<KiottyNetHttpMethod>(index);
                const Span<const char> text = methodTextOf(method);
                const bool is_same = text.length > 0 && equalsLiteral(token, text.data);
                if (is_same) { return KIOTTY_NET_HTTP_METHOD_BIT(method); }
            }
            return 0;
        }
    }
}
