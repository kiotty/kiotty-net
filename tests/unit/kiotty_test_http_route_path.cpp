#include "domain/usecases/http/kiotty_http_route_path.h"

#include "kiotty_net_http.h"

#include <gtest/gtest.h>

#include <string>

using kiotty::net::implementation::hasParentSegment;
using kiotty::net::implementation::isSafeRelativePath;
using kiotty::net::implementation::matchesPath;
using kiotty::net::implementation::parseDirectoryPrefix;
using kiotty::net::implementation::parseRoutePath;
using kiotty::net::implementation::PathKind;
using kiotty::net::implementation::RoutePath;
using kiotty::net::implementation::RoutePathResult;
using kiotty::net::implementation::Span;

namespace
{
    std::string textOf(Span<const char> span)
    {
        return std::string(span.data, span.length);
    }

    KiottyNetCode routeCodeOf(const std::string& text)
    {
        const RoutePathResult parsed = parseRoutePath(text.c_str());
        return parsed.code;
    }

    bool matches(const char* route, const std::string& path)
    {
        const RoutePathResult parsed = parseRoutePath(route);
        const Span<const char> request{ path.data(), path.size() };
        return parsed && matchesPath(parsed.value, request);
    }

    bool matchesDirectory(const char* prefix, const std::string& path)
    {
        const RoutePathResult parsed = parseDirectoryPrefix(prefix);
        const Span<const char> request{ path.data(), path.size() };
        return parsed && matchesPath(parsed.value, request);
    }
}

TEST(HttpRoutePath, EmptyInputIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf(""));
}

TEST(HttpRoutePath, PathOfTheMaximumLengthIsAccepted)
{
    const std::string path = "/" + std::string(KIOTTY_NET_MAX_URL_LENGTH - 2, 'a');
    const RoutePathResult parsed = parseRoutePath(path.c_str());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(path, textOf(parsed.value.text));
}

TEST(HttpRoutePath, PathOneOverTheMaximumIsInvalidArgument)
{
    const std::string path = "/" + std::string(KIOTTY_NET_MAX_URL_LENGTH - 1, 'a');
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf(path));
}

TEST(HttpRoutePath, MiddleNullByteEndsThePath)
{
    const char text[] = { '/', 'a', '\0', 'b', '\0' };
    const RoutePathResult parsed = parseRoutePath(text);
    ASSERT_TRUE(parsed);
    EXPECT_EQ("/a", textOf(parsed.value.text));
}

TEST(HttpRoutePath, InvalidUtf8IsInvalidEncoding)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, routeCodeOf("/\xC3\x28"));
}

TEST(HttpRoutePath, NullTextIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, parseRoutePath(nullptr).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, parseDirectoryPrefix(nullptr).code);
}

TEST(HttpRoutePath, PathsThatARequestCannotCarryAreInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("api"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("/a b"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("/a?b"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("/a#b"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("/\xED\x99\x8D"));
}

TEST(HttpRoutePath, StarIsOnlyAWholeLastSegment)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("/a*"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("/a/*/b"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, routeCodeOf("*"));
    const RoutePathResult parsed = parseRoutePath("/api/devices/*");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(PathKind::PREFIX, parsed.value.kind);
    EXPECT_EQ("/api/devices/", textOf(parsed.value.text));
}

TEST(HttpRoutePath, ExactPathMatchesOnlyItselfWithCase)
{
    EXPECT_TRUE(matches("/health", "/health"));
    EXPECT_FALSE(matches("/health", "/health/"));
    EXPECT_FALSE(matches("/health", "/Health"));
    EXPECT_FALSE(matches("/health", "/heal"));
}

TEST(HttpRoutePath, WildcardMatchesEverythingUnderTheSlash)
{
    EXPECT_TRUE(matches("/api/*", "/api/"));
    EXPECT_TRUE(matches("/api/*", "/api/devices/7"));
    EXPECT_FALSE(matches("/api/*", "/api"));
    EXPECT_FALSE(matches("/api/*", "/apix"));
    EXPECT_TRUE(matches("/*", "/anything"));
}

TEST(HttpDirectoryPrefix, MustEndWithASlash)
{
    EXPECT_TRUE(parseDirectoryPrefix("/static/"));
    EXPECT_TRUE(parseDirectoryPrefix("/"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, parseDirectoryPrefix("/static").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, parseDirectoryPrefix("/static/*").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, parseDirectoryPrefix("").code);
}

TEST(HttpDirectoryPrefix, MatchesPathsUnderThePrefix)
{
    EXPECT_TRUE(matchesDirectory("/static/", "/static/a/b.js"));
    EXPECT_FALSE(matchesDirectory("/static/", "/static"));
    EXPECT_TRUE(matchesDirectory("/", "/index.html"));
}

namespace
{
    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }
}

TEST(HttpRelativePath, ParentSegmentsAreFoundWithEitherSeparator)
{
    EXPECT_TRUE(hasParentSegment(spanOf("..")));
    EXPECT_TRUE(hasParentSegment(spanOf("a/../b")));
    EXPECT_TRUE(hasParentSegment(spanOf("a\\..\\b")));
    EXPECT_TRUE(hasParentSegment(spanOf("a/..")));
    EXPECT_FALSE(hasParentSegment(spanOf("")));
    EXPECT_FALSE(hasParentSegment(spanOf("a..b/c")));
    EXPECT_FALSE(hasParentSegment(spanOf(".../.")));
    EXPECT_FALSE(hasParentSegment(spanOf("C:\\www\\a.txt")));
}

TEST(HttpRelativePath, SafeRelativePathRefusesParentsBackslashesColonsAndNul)
{
    EXPECT_TRUE(isSafeRelativePath(spanOf("")));
    EXPECT_TRUE(isSafeRelativePath(spanOf("js/app.js")));
    EXPECT_TRUE(isSafeRelativePath(spanOf("index..html")));
    EXPECT_FALSE(isSafeRelativePath(spanOf("../secret")));
    EXPECT_FALSE(isSafeRelativePath(spanOf("a\\b")));
    EXPECT_FALSE(isSafeRelativePath(spanOf("c:x")));
    EXPECT_FALSE(isSafeRelativePath(spanOf(std::string("a\0b", 3))));
}
