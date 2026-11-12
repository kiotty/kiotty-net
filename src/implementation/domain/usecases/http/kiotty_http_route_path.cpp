#include "domain/usecases/http/kiotty_http_route_path.h"

#include "core/kiotty_text.h"
#include "domain/usecases/http/kiotty_http_token.h"
#include "kiotty_net_http.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_PATH_LENGTH = KIOTTY_NET_MAX_URL_LENGTH - 1;
        constexpr size_t WILDCARD_LENGTH = 1;
        constexpr KiottyNetCode INVALID_ARGUMENT = KIOTTY_NET_CODE_INVALID_ARGUMENT;

        RoutePathResult parsePathText(const char* text);
        bool            isPathText(Span<const char> text);
        bool            hasWildcardSuffix(Span<const char> text);
        bool            endsWithSlash(Span<const char> text);
        bool            isParentSegment(Span<const char> path, size_t start, size_t end);
        bool            isUnsafeCharacter(char character);
    }

    RoutePathResult parseRoutePath(const char* text)
    {
        const RoutePathResult parsed = parsePathText(text);
        if (!parsed) { return parsed; }
        const Span<const char> path = parsed.value.text;
        const bool is_prefix = hasWildcardSuffix(path);
        if (!is_prefix) { return RoutePathResult::success(RoutePath{ path, PathKind::EXACT }); }
        const Span<const char> prefix{ path.data, path.length - WILDCARD_LENGTH };
        return RoutePathResult::success(RoutePath{ prefix, PathKind::PREFIX });
    }

    RoutePathResult parseDirectoryPrefix(const char* text)
    {
        const RoutePathResult parsed = parsePathText(text);
        if (!parsed) { return parsed; }
        const Span<const char> path = parsed.value.text;
        if (!endsWithSlash(path)) { return RoutePathResult::failure(INVALID_ARGUMENT); }
        return RoutePathResult::success(RoutePath{ path, PathKind::PREFIX });
    }

    bool matchesPath(const RoutePath& route, Span<const char> path)
    {
        const size_t length = route.text.length;
        const bool is_exact = route.kind == PathKind::EXACT;
        if (is_exact && path.length != length) { return false; }
        if (path.length < length) { return false; }
        return std::memcmp(path.data, route.text.data, length) == 0;
    }

    bool hasParentSegment(Span<const char> path)
    {
        size_t segment_start = 0;
        for (size_t i = 0; i <= path.length; ++i)
        {
            const bool is_end = i == path.length || path[i] == '/' || path[i] == '\\';
            if (is_end && isParentSegment(path, segment_start, i)) { return true; }
            if (is_end) { segment_start = i + 1; }
        }
        return false;
    }

    bool isSafeRelativePath(Span<const char> path)
    {
        if (hasParentSegment(path)) { return false; }
        for (size_t i = 0; i < path.length; ++i)
        {
            if (isUnsafeCharacter(path[i])) { return false; }
        }
        return true;
    }

    namespace
    {
        RoutePathResult parsePathText(const char* text)
        {
            const TextResult parsed = parseText(text, MAX_PATH_LENGTH);
            if (!parsed) { return RoutePathResult::failure(parsed.code); }
            if (!isPathText(parsed.value)) { return RoutePathResult::failure(INVALID_ARGUMENT); }
            return RoutePathResult::success(RoutePath{ parsed.value, PathKind::EXACT });
        }

        bool isPathText(Span<const char> text)
        {
            if (text.empty()) { return false; }
            if (text[0] != '/') { return false; }
            const size_t star_position = hasWildcardSuffix(text) ? text.length - 1 : text.length;
            for (size_t i = 0; i < text.length; ++i)
            {
                const bool is_reserved = text[i] == '?' || text[i] == '#';
                const bool is_misplaced_star = text[i] == '*' && i != star_position;
                const bool is_allowed = isVisibleCharacter(text[i]) && !is_reserved;
                if (!is_allowed || is_misplaced_star) { return false; }
            }
            return true;
        }

        bool hasWildcardSuffix(Span<const char> text)
        {
            const bool is_long_enough = text.length >= 2;
            if (!is_long_enough) { return false; }
            return text[text.length - 1] == '*' && text[text.length - 2] == '/';
        }

        bool endsWithSlash(Span<const char> text)
        {
            return text[text.length - 1] == '/';
        }

        bool isParentSegment(Span<const char> path, size_t start, size_t end)
        {
            const bool is_two = end - start == 2;
            return is_two && path[start] == '.' && path[start + 1] == '.';
        }

        bool isUnsafeCharacter(char character)
        {
            return character == '\\' || character == ':' || character == '\0';
        }
    }
}
