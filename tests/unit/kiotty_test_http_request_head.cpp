#include "domain/usecases/http/kiotty_http_request_check.h"
#include "domain/usecases/http/kiotty_http_request_head.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using kiotty::net::implementation::buildHostField;
using kiotty::net::implementation::checkRequestBody;
using kiotty::net::implementation::checkRequestHeader;
using kiotty::net::implementation::checkRequestPath;
using kiotty::net::implementation::HeadLengthResult;
using kiotty::net::implementation::HOST_FIELD_BYTES;
using kiotty::net::implementation::HostFieldResult;
using kiotty::net::implementation::HTTP_DEFAULT_PORT;
using kiotty::net::implementation::HTTPS_DEFAULT_PORT;
using kiotty::net::implementation::RequestHead;
using kiotty::net::implementation::Span;
using kiotty::net::implementation::writeRequestHead;

namespace
{
    constexpr size_t WIDE = 1024;

    struct Field
    {
        std::string name;
        std::string value;
    };

    struct Request
    {
        KiottyNetHttpMethod      method = KIOTTY_NET_HTTP_METHOD_GET;
        std::string              path = "/";
        std::string              host = "example.com";
        std::vector<Field>       shared;
        std::vector<Field>       extra;
        KiottyNetHttpContentType type = KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM;
        size_t                   body_length = 0;
        size_t                   max_headers = 16;
    };

    struct Written
    {
        KiottyNetCode code;
        std::string   head;
    };

    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    KiottyNetHttpHeader headerOf(const Field& field)
    {
        const KiottyNetSpan name{ field.name.data(), field.name.size() };
        const KiottyNetSpan value{ field.value.data(), field.value.size() };
        return KiottyNetHttpHeader{ name, value };
    }

    std::vector<KiottyNetHttpHeader> headersOf(const std::vector<Field>& fields)
    {
        std::vector<KiottyNetHttpHeader> headers;
        for (const Field& field : fields)
        {
            headers.push_back(headerOf(field));
        }
        return headers;
    }

    Written write(const Request& request, size_t buffer_bytes)
    {
        const std::vector<KiottyNetHttpHeader> shared = headersOf(request.shared);
        const std::vector<KiottyNetHttpHeader> extra = headersOf(request.extra);
        const RequestHead head{
            Span<const KiottyNetHttpHeader>{ shared.data(), shared.size() },
            Span<const KiottyNetHttpHeader>{ extra.data(), extra.size() },
            spanOf(request.path),
            spanOf(request.host),
            request.body_length,
            request.max_headers,
            request.method,
            request.type,
        };
        std::vector<char> buffer(buffer_bytes);
        const HeadLengthResult written = writeRequestHead(Span<char>{ buffer.data(), buffer.size() }, head);
        if (!written) { return Written{ written.code, "" }; }
        return Written{ written.code, std::string(buffer.data(), written.value) };
    }

    std::string headOf(const Request& request)
    {
        const Written written = write(request, WIDE);
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, written.code);
        return written.head;
    }

    KiottyNetCode codeOf(const Request& request)
    {
        return write(request, WIDE).code;
    }

    KiottyNetCode headerCodeOf(const std::string& name, const std::string& value)
    {
        return checkRequestHeader(headerOf(Field{ name, value }));
    }

    std::string hostOf(const std::string& host, uint16_t port, uint16_t default_port)
    {
        const HostFieldResult field = buildHostField(spanOf(host), port, default_port);
        EXPECT_TRUE(field);
        return std::string(field.value.span().data, field.value.span().length);
    }

    KiottyNetCode hostCodeOf(const std::string& host)
    {
        return buildHostField(spanOf(host), HTTP_DEFAULT_PORT, HTTP_DEFAULT_PORT).code;
    }
}

TEST(HttpRequestHead, GetHasRequestLineAndHostOnly)
{
    Request request;
    request.path = "/api/status?x=1";
    EXPECT_EQ("GET /api/status?x=1 HTTP/1.1\r\nHost: example.com\r\n\r\n", headOf(request));
}

TEST(HttpRequestHead, PostWithBodyHasContentTypeAndLength)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_POST;
    request.path = "/api/report";
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    request.body_length = 13;
    EXPECT_EQ("POST /api/report HTTP/1.1\r\nHost: example.com\r\n"
              "Content-Type: application/json\r\nContent-Length: 13\r\n\r\n",
              headOf(request));
}

TEST(HttpRequestHead, PostPutPatchWithoutBodySendContentLengthZero)
{
    for (const KiottyNetHttpMethod method :
         { KIOTTY_NET_HTTP_METHOD_POST, KIOTTY_NET_HTTP_METHOD_PUT, KIOTTY_NET_HTTP_METHOD_PATCH })
    {
        Request request;
        request.method = method;
        request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
        const std::string head = headOf(request);
        EXPECT_NE(std::string::npos, head.find("\r\nContent-Length: 0\r\n\r\n"));
        EXPECT_EQ(std::string::npos, head.find("Content-Type"));
    }
}

TEST(HttpRequestHead, DeleteWithBodySendsItsLength)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_DELETE;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN;
    request.body_length = 2;
    EXPECT_EQ("DELETE / HTTP/1.1\r\nHost: example.com\r\n"
              "Content-Type: text/plain; charset=utf-8\r\nContent-Length: 2\r\n\r\n",
              headOf(request));
}

TEST(HttpRequestHead, OptionsAsteriskTargetsTheWholeServer)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_OPTIONS;
    request.path = "*";
    EXPECT_EQ("OPTIONS * HTTP/1.1\r\nHost: example.com\r\n\r\n", headOf(request));
}

TEST(HttpRequestHead, SharedHeadersComeBeforeExtraHeadersAndExtraOverridesTheSameName)
{
    Request request;
    request.shared = { { "Authorization", "Bearer a" }, { "User-Agent", "device/1.0" } };
    request.extra = { { "authorization", "Bearer b" }, { "Accept", "application/json" } };
    EXPECT_EQ("GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: device/1.0\r\n"
              "authorization: Bearer b\r\nAccept: application/json\r\n\r\n",
              headOf(request));
}

TEST(HttpRequestHead, ExtraHeaderValuesAreTrimmed)
{
    Request request;
    request.extra = { { "Accept", " \t text/event-stream \t " } };
    EXPECT_EQ("GET / HTTP/1.1\r\nHost: example.com\r\nAccept: text/event-stream\r\n\r\n",
              headOf(request));
}

TEST(HttpRequestHead, CustomTypeLeavesContentTypeToTheHeaders)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_PUT;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM;
    request.body_length = 4;
    request.extra = { { "Content-Type", "application/x-thing" } };
    EXPECT_EQ("PUT / HTTP/1.1\r\nHost: example.com\r\nContent-Type: application/x-thing\r\n"
              "Content-Length: 4\r\n\r\n",
              headOf(request));
}

TEST(HttpRequestHead, CustomTypeWithoutAHeaderSendsNoContentType)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_POST;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM;
    request.body_length = 1;
    EXPECT_EQ("POST / HTTP/1.1\r\nHost: example.com\r\nContent-Length: 1\r\n\r\n", headOf(request));
}

TEST(HttpRequestHead, ExtraContentTypeReplacesTheTypedOne)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_POST;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    request.body_length = 2;
    request.extra = { { "content-type", "application/json; charset=utf-8" } };
    const std::string head = headOf(request);
    EXPECT_EQ(std::string::npos, head.find("Content-Type: application/json\r\n"));
    EXPECT_NE(std::string::npos, head.find("content-type: application/json; charset=utf-8\r\n"));
}

TEST(HttpRequestHead, TypedBodyDropsASharedContentType)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_POST;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    request.body_length = 2;
    request.shared = { { "Content-Type", "text/plain" } };
    EXPECT_EQ("POST / HTTP/1.1\r\nHost: example.com\r\n"
              "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n",
              headOf(request));
}

TEST(HttpRequestHead, HeaderInjectionIsInvalidArgument)
{
    for (const std::string& value :
         { std::string("a\r\nX-Evil: 1"), std::string("a\nb"), std::string("a\rb"),
           std::string("a\0b", 3), std::string("a\x7F"), std::string("a\x01") })
    {
        Request request;
        request.extra = { { "X-Value", value } };
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, codeOf(request));
    }
}

TEST(HttpRequestHead, BadHeaderNamesAreInvalidArgument)
{
    for (const std::string name : { "", "Bad Name", "Na:me", "Na(me", "\xC3\xA9", "X\r\n" })
    {
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, headerCodeOf(name, "v"));
    }
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, headerCodeOf("X-Ok_1.2~!#$%&'*+^`|", "v"));
}

TEST(HttpRequestHead, LibraryOwnedHeadersAreInvalidArgument)
{
    for (const std::string name : { "Host", "content-length", "TRANSFER-ENCODING", "Connection" })
    {
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, headerCodeOf(name, "v"));
        Request request;
        request.extra = { { name, "v" } };
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, codeOf(request));
    }
}

TEST(HttpRequestHead, HeaderValuesAllowUtf8TabsAndEmptyButNotBrokenUtf8)
{
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, headerCodeOf("X-Name", "\xED\x99\x8D\tv"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, headerCodeOf("X-Name", ""));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, headerCodeOf("X-Name", "\xC3\x28"));
}

TEST(HttpRequestHead, NullDataWithALengthIsInvalidArgument)
{
    const KiottyNetSpan name{ "X-A", 3 };
    const KiottyNetSpan missing{ nullptr, 2 };
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkRequestHeader(KiottyNetHttpHeader{ name, missing }));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkRequestHeader(KiottyNetHttpHeader{ missing, name }));
    const KiottyNetSpan empty{ nullptr, 0 };
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkRequestHeader(KiottyNetHttpHeader{ name, empty }));
}

TEST(HttpRequestHead, PathsMustBeOriginFormVisibleAscii)
{
    const KiottyNetHttpMethod get = KIOTTY_NET_HTTP_METHOD_GET;
    for (const std::string path : { "", "api", "/a b", "/a\r\n", "/a\tb", "*", "/\xC3\xA9" })
    {
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkRequestPath(get, spanOf(path)));
    }
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkRequestPath(get, spanOf("/a/b?x=1&y=%20")));
    const KiottyNetHttpMethod options = KIOTTY_NET_HTTP_METHOD_OPTIONS;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkRequestPath(options, spanOf("*")));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkRequestPath(options, spanOf("**")));
    Request request;
    request.path = "relative";
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, codeOf(request));
}

TEST(HttpRequestHead, UnknownMethodIsInvalidArgument)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_COUNT;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, codeOf(request));
}

TEST(HttpRequestHead, BodyTypeAndEncodingAreChecked)
{
    const std::string broken = "\xC3\x28";
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT,
              checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION, spanOf("")));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT,
              checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT, spanOf("")));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING,
              checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf(broken)));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING,
              checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, spanOf(broken)));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_PNG, spanOf(broken)));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM, spanOf(broken)));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS,
              checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf("{\"a\":\"\xED\x99\x8D\"}")));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT,
              checkRequestBody(KIOTTY_NET_HTTP_CONTENT_TYPE_PNG, Span<const char>{ nullptr, 3 }));
}

TEST(HttpRequestHead, HeadThatFitsExactlyIsWrittenAndOneByteLessIsRequestTooLarge)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_POST;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    request.body_length = 1234;
    request.shared = { { "Authorization", "Bearer token" } };
    request.extra = { { "Accept", "application/json" } };
    const std::string head = headOf(request);
    const Written exact = write(request, head.size());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, exact.code);
    EXPECT_EQ(head, exact.head);
    for (size_t bytes = 0; bytes < head.size(); ++bytes)
    {
        EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, write(request, bytes).code) << bytes;
    }
}

TEST(HttpRequestHead, HeaderCountAtMaxHeadersIsWrittenAndOneMoreIsTableFull)
{
    Request request;
    request.method = KIOTTY_NET_HTTP_METHOD_POST;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_JSON;
    request.body_length = 3;
    request.shared = { { "Authorization", "a" }, { "Accept", "x" } };
    request.extra = { { "accept", "y" }, { "X-Id", "1" } };
    request.max_headers = 6;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf(request));
    request.max_headers = 5;
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, codeOf(request));
}

TEST(HttpRequestHead, MoreExtraHeadersThanMaxHeadersIsTableFull)
{
    Request request;
    request.extra = { { "A", "1" }, { "B", "2" }, { "C", "3" } };
    request.max_headers = 2;
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, codeOf(request));
}

TEST(HttpHostField, DefaultPortIsLeftOut)
{
    EXPECT_EQ("example.com", hostOf("example.com", HTTP_DEFAULT_PORT, HTTP_DEFAULT_PORT));
    EXPECT_EQ("example.com", hostOf("example.com", HTTPS_DEFAULT_PORT, HTTPS_DEFAULT_PORT));
    EXPECT_EQ("10.0.0.7", hostOf("10.0.0.7", HTTP_DEFAULT_PORT, HTTP_DEFAULT_PORT));
}

TEST(HttpHostField, OtherPortsAreAppended)
{
    EXPECT_EQ("example.com:8080", hostOf("example.com", 8080, HTTP_DEFAULT_PORT));
    EXPECT_EQ("example.com:80", hostOf("example.com", HTTP_DEFAULT_PORT, HTTPS_DEFAULT_PORT));
    EXPECT_EQ("example.com:65535", hostOf("example.com", 65535, HTTP_DEFAULT_PORT));
}

TEST(HttpHostField, Ipv6LiteralsAreBracketed)
{
    EXPECT_EQ("[::1]:8080", hostOf("::1", 8080, HTTP_DEFAULT_PORT));
    EXPECT_EQ("[::1]", hostOf("::1", HTTPS_DEFAULT_PORT, HTTPS_DEFAULT_PORT));
    EXPECT_EQ("[fe80::1]", hostOf("[fe80::1]", HTTP_DEFAULT_PORT, HTTP_DEFAULT_PORT));
}

TEST(HttpHostField, EmptyOrInvisibleHostIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, hostCodeOf(""));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, hostCodeOf("a b"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, hostCodeOf("a\r\nX: 1"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, hostCodeOf(std::string("a\0b", 3)));
}

TEST(HttpHostField, LongestAddressHostFitsAndAnOverlongHostIsInvalidArgument)
{
    const std::string longest(KIOTTY_NET_MAX_HOST_LENGTH - 1, ':');
    const HostFieldResult field = buildHostField(spanOf(longest), 65535, HTTP_DEFAULT_PORT);
    ASSERT_TRUE(field);
    EXPECT_EQ(longest.size() + 8, field.value.length());
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, hostCodeOf(std::string(HOST_FIELD_BYTES + 1, 'a')));
}
