#include "http/data/repository/header/kiotty_http_reply_headers_parser.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using kiotty::net::implementation::allowedMethodsOf;
using kiotty::net::implementation::findReplyHeader;
using kiotty::net::implementation::Span;

namespace
{
    struct Field
    {
        std::string name;
        std::string value;
    };

    struct Headers
    {
        explicit Headers(std::vector<Field> fields)
            : texts(std::move(fields))
            , entries()
        {
            for (const Field& field : texts)
            {
                const KiottyNetSpan name{ field.name.data(), field.name.size() };
                const KiottyNetSpan value{ field.value.data(), field.value.size() };
                entries.push_back(KiottyNetHttpHeader{ name, value });
            }
        }

        Span<const KiottyNetHttpHeader> span() const
        {
            return Span<const KiottyNetHttpHeader>{ entries.data(), entries.size() };
        }

        std::vector<Field>               texts;
        std::vector<KiottyNetHttpHeader> entries;
    };

    std::string found(const Headers& headers, const char* name)
    {
        const Span<const char> wanted{ name, std::char_traits<char>::length(name) };
        const KiottyNetSpan value = findReplyHeader(headers.span(), wanted);
        if (value.data == nullptr) { return "<none>"; }
        return std::string(value.data, value.length);
    }

    uint32_t bitOf(KiottyNetHttpMethod method)
    {
        return KIOTTY_NET_HTTP_METHOD_BIT(method);
    }
}

TEST(HttpReplyHeaders, FindIgnoresCaseAndReturnsTheFirstMatch)
{
    const Headers headers({ { "Content-Type", "text/plain" }, { "X-A", "1" }, { "x-a", "2" } });
    EXPECT_EQ("text/plain", found(headers, "content-type"));
    EXPECT_EQ("1", found(headers, "X-A"));
    EXPECT_EQ("<none>", found(headers, "X-B"));
    EXPECT_EQ("<none>", found(Headers({}), "X-A"));
}

TEST(HttpReplyHeaders, AllowListBecomesMethodBits)
{
    const Headers headers({ { "Allow", "GET, HEAD,OPTIONS ,  POST" } });
    const uint32_t expected = bitOf(KIOTTY_NET_HTTP_METHOD_GET) | bitOf(KIOTTY_NET_HTTP_METHOD_HEAD)
                              | bitOf(KIOTTY_NET_HTTP_METHOD_OPTIONS) | bitOf(KIOTTY_NET_HTTP_METHOD_POST);
    EXPECT_EQ(expected, allowedMethodsOf(headers.span()));
}

TEST(HttpReplyHeaders, AllowIgnoresUnknownLowerCaseAndEmptyItems)
{
    const Headers headers({ { "allow", "get, PROPFIND, , DELETE,,TRACE" } });
    EXPECT_EQ(bitOf(KIOTTY_NET_HTTP_METHOD_DELETE), allowedMethodsOf(headers.span()));
}

TEST(HttpReplyHeaders, SeveralAllowLinesAreJoinedAndOtherHeadersIgnored)
{
    const Headers headers({ { "Allow", "PUT" }, { "X-Allow", "GET" }, { "Allow", "PATCH" } });
    const uint32_t expected = bitOf(KIOTTY_NET_HTTP_METHOD_PUT) | bitOf(KIOTTY_NET_HTTP_METHOD_PATCH);
    EXPECT_EQ(expected, allowedMethodsOf(headers.span()));
}

TEST(HttpReplyHeaders, MissingOrEmptyAllowIsZero)
{
    EXPECT_EQ(0u, allowedMethodsOf(Headers({}).span()));
    EXPECT_EQ(0u, allowedMethodsOf(Headers({ { "Allow", "" } }).span()));
}
