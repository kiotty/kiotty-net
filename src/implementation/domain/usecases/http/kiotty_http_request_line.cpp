#include "domain/usecases/http/kiotty_http_request_line.h"

#include "domain/usecases/http/kiotty_http_token.h"

#include <cstring>
#include <string>

namespace kiotty::net::implementation
{
    namespace
    {
        struct RequestParts
        {
            Span<const char> method;
            Span<const char> target;
            Span<const char> version;
        };

        struct RequestTarget
        {
            Span<const char> path;
            Span<const char> query;
        };

        struct MethodName
        {
            const char*         text;
            KiottyNetHttpMethod method;
        };

        using PartsResult = Result<RequestParts, KiottyNetHttpStatus>;
        using MethodResult = Result<KiottyNetHttpMethod, KiottyNetHttpStatus>;
        using TargetResult = Result<RequestTarget, KiottyNetHttpStatus>;
        using VersionResult = Result<HttpVersion, KiottyNetHttpStatus>;

        constexpr MethodName METHOD_NAMES[] = {
            { "GET", KIOTTY_NET_HTTP_METHOD_GET },
            { "HEAD", KIOTTY_NET_HTTP_METHOD_HEAD },
            { "POST", KIOTTY_NET_HTTP_METHOD_POST },
            { "PUT", KIOTTY_NET_HTTP_METHOD_PUT },
            { "PATCH", KIOTTY_NET_HTTP_METHOD_PATCH },
            { "DELETE", KIOTTY_NET_HTTP_METHOD_DELETE },
            { "OPTIONS", KIOTTY_NET_HTTP_METHOD_OPTIONS },
        };
        constexpr KiottyNetHttpStatus BAD_REQUEST = KIOTTY_NET_HTTP_STATUS_BAD_REQUEST;
        constexpr KiottyNetHttpStatus NOT_IMPLEMENTED = KIOTTY_NET_HTTP_STATUS_NOT_IMPLEMENTED;
        constexpr KiottyNetHttpStatus URI_TOO_LONG = KIOTTY_NET_HTTP_STATUS_URI_TOO_LONG;
        constexpr char VERSION_1_0[] = "HTTP/1.0";
        constexpr char VERSION_1_1[] = "HTTP/1.1";
        constexpr char ASTERISK[] = "*";

        PartsResult   splitRequestLine(Span<const char> line);
        MethodResult  parseMethod(Span<const char> text);
        TargetResult  parseTarget(Span<const char> text, KiottyNetHttpMethod method);
        VersionResult parseVersion(Span<const char> text);
        bool          isTargetText(Span<const char> text);
        bool          equalsExactly(Span<const char> text, const char* literal);
    }

    RequestLineResult parseRequestLine(Span<const char> line)
    {
        const PartsResult parts = splitRequestLine(line);
        if (!parts) { return RequestLineResult::failure(parts.code); }
        const MethodResult method = parseMethod(parts.value.method);
        if (!method) { return RequestLineResult::failure(method.code); }
        const TargetResult target = parseTarget(parts.value.target, method.value);
        if (!target) { return RequestLineResult::failure(target.code); }
        const VersionResult version = parseVersion(parts.value.version);
        if (!version) { return RequestLineResult::failure(version.code); }
        const RequestTarget& found = target.value;
        const RequestLine parsed{ found.path, found.query, method.value, version.value };
        return RequestLineResult::success(parsed);
    }

    namespace
    {
        PartsResult splitRequestLine(Span<const char> line)
        {
            const size_t first = findCharacter(line, ' ', 0);
            const size_t second = findCharacter(line, ' ', first + 1);
            const bool has_two_spaces = second < line.length;
            if (!has_two_spaces) { return PartsResult::failure(BAD_REQUEST); }
            const Span<const char> method{ line.data, first };
            const Span<const char> target{ line.data + first + 1, second - first - 1 };
            const Span<const char> version = line.skip(second + 1);
            const bool has_empty_part = method.empty() || target.empty() || version.empty();
            if (has_empty_part) { return PartsResult::failure(BAD_REQUEST); }
            return PartsResult::success(RequestParts{ method, target, version });
        }

        MethodResult parseMethod(Span<const char> text)
        {
            if (!isToken(text)) { return MethodResult::failure(BAD_REQUEST); }
            for (const MethodName& name : METHOD_NAMES)
            {
                if (equalsExactly(text, name.text)) { return MethodResult::success(name.method); }
            }
            return MethodResult::failure(NOT_IMPLEMENTED);
        }

        TargetResult parseTarget(Span<const char> text, KiottyNetHttpMethod method)
        {
            const bool is_too_long = text.length >= KIOTTY_NET_MAX_URL_LENGTH;
            if (is_too_long) { return TargetResult::failure(URI_TOO_LONG); }
            if (!isTargetText(text)) { return TargetResult::failure(BAD_REQUEST); }
            const bool is_asterisk = equalsExactly(text, ASTERISK);
            const bool is_options = method == KIOTTY_NET_HTTP_METHOD_OPTIONS;
            const RequestTarget whole_server{ text, {} };
            if (is_asterisk && is_options) { return TargetResult::success(whole_server); }
            const bool is_origin_form = text[0] == '/';
            if (!is_origin_form) { return TargetResult::failure(BAD_REQUEST); }
            const size_t mark = findCharacter(text, '?', 0);
            const Span<const char> path{ text.data, mark };
            const Span<const char> query = text.skip(mark + 1);
            return TargetResult::success(RequestTarget{ path, query });
        }

        VersionResult parseVersion(Span<const char> text)
        {
            const bool is_one_one = equalsExactly(text, VERSION_1_1);
            if (is_one_one) { return VersionResult::success(HttpVersion::V11); }
            const bool is_one_zero = equalsExactly(text, VERSION_1_0);
            if (is_one_zero) { return VersionResult::success(HttpVersion::V10); }
            return VersionResult::failure(BAD_REQUEST);
        }

        bool isTargetText(Span<const char> text)
        {
            for (size_t i = 0; i < text.length; ++i)
            {
                const bool is_allowed = isVisibleCharacter(text[i]) && text[i] != '#';
                if (!is_allowed) { return false; }
            }
            return true;
        }

        bool equalsExactly(Span<const char> text, const char* literal)
        {
            const size_t length = std::char_traits<char>::length(literal);
            if (text.length != length) { return false; }
            return std::memcmp(text.data, literal, length) == 0;
        }
    }
}
