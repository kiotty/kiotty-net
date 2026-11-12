#include "domain/usecases/http/kiotty_http_content_type.h"

#include <gtest/gtest.h>

#include <string>

using kiotty::net::implementation::contentTypeOfPath;
using kiotty::net::implementation::contentTypeTextOf;
using kiotty::net::implementation::parseCustomContentType;
using kiotty::net::implementation::Span;
using kiotty::net::implementation::TextKind;
using kiotty::net::implementation::textKindOf;
using kiotty::net::implementation::TextResult;

namespace
{
    std::string textOf(Span<const char> span)
    {
        return std::string(span.data, span.length);
    }

    std::string typeTextOf(KiottyNetHttpContentType type)
    {
        return textOf(contentTypeTextOf(type));
    }

    KiottyNetHttpContentType typeOfPath(const std::string& path)
    {
        return contentTypeOfPath(Span<const char>{ path.data(), path.size() });
    }

    KiottyNetCode customCodeOf(const std::string& text)
    {
        const TextResult parsed = parseCustomContentType(text.c_str());
        return parsed.code;
    }

    std::string customOfLength(size_t length)
    {
        return "a/" + std::string(length - 2, 'b');
    }
}

TEST(HttpContentTypeTable, EveryNamedTypeHasAMediaTypeText)
{
    for (int i = 0; i < KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION; ++i)
    {
        const std::string text = typeTextOf(static_cast<KiottyNetHttpContentType>(i));
        EXPECT_NE(std::string::npos, text.find('/')) << i;
        EXPECT_EQ(std::string::npos, text.find('\r')) << i;
    }
}

TEST(HttpContentTypeTable, TextTypesCarryTheCharsetAndStructuredTypesDoNot)
{
    EXPECT_EQ("text/plain; charset=utf-8", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN));
    EXPECT_EQ("text/event-stream; charset=utf-8", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM));
    EXPECT_EQ("application/json", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON));
    EXPECT_EQ("application/dicom+json", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM_JSON));
    EXPECT_EQ("application/wasm", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_WASM));
}

TEST(HttpContentTypeTable, PlaceholdersHaveNoText)
{
    EXPECT_EQ("", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION));
    EXPECT_EQ("", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM));
    EXPECT_EQ("", typeTextOf(KIOTTY_NET_HTTP_CONTENT_TYPE_COUNT));
    EXPECT_EQ(TextKind::BINARY, textKindOf(KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM));
}

TEST(HttpContentTypeTable, TextKindTellsWhichBodiesAreUtf8)
{
    EXPECT_EQ(TextKind::UTF8_WITH_CHARSET, textKindOf(KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML));
    EXPECT_EQ(TextKind::UTF8, textKindOf(KIOTTY_NET_HTTP_CONTENT_TYPE_FHIR_XML));
    EXPECT_EQ(TextKind::UTF8, textKindOf(KIOTTY_NET_HTTP_CONTENT_TYPE_SVG));
    EXPECT_EQ(TextKind::BINARY, textKindOf(KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM));
    EXPECT_EQ(TextKind::BINARY, textKindOf(KIOTTY_NET_HTTP_CONTENT_TYPE_PNG));
}

TEST(HttpContentTypeOfPath, ExtensionPicksTheTypeIgnoringCase)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_HTML, typeOfPath("./www/index.HTML"));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_JAVASCRIPT, typeOfPath("/app/main.mjs"));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_GZIP, typeOfPath("logs.tar.gz"));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_DICOM, typeOfPath("C:\\study\\image.dcm"));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_WOFF2, typeOfPath("font.woff2"));
}

TEST(HttpContentTypeOfPath, UnknownOrMissingExtensionIsOctetStream)
{
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, typeOfPath(""));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, typeOfPath("README"));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, typeOfPath("dir.d/file"));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, typeOfPath("file."));
    EXPECT_EQ(KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, typeOfPath("archive.rar"));
}

TEST(HttpCustomContentType, EmptyInputIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf(""));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, parseCustomContentType(nullptr).code);
}

TEST(HttpCustomContentType, NameOfTheMaximumLengthIsAccepted)
{
    const std::string name = customOfLength(KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH - 1);
    const TextResult parsed = parseCustomContentType(name.c_str());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(name, textOf(parsed.value));
}

TEST(HttpCustomContentType, NameOneOverTheMaximumIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf(customOfLength(KIOTTY_NET_MAX_CONTENT_TYPE_LENGTH)));
}

TEST(HttpCustomContentType, MiddleNullByteEndsTheName)
{
    const char text[] = { 't', 'e', 'x', 't', '/', 'x', '\0', ';', '\0' };
    const TextResult parsed = parseCustomContentType(text);
    ASSERT_TRUE(parsed);
    EXPECT_EQ("text/x", textOf(parsed.value));
}

TEST(HttpCustomContentType, InvalidUtf8IsInvalidEncoding)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, customCodeOf("text/\xC3\x28"));
}

TEST(HttpCustomContentType, DicomWebMultipartWithParametersIsAccepted)
{
    EXPECT_TRUE(parseCustomContentType("multipart/related; type=\"application/dicom\"; boundary=b1"));
    EXPECT_TRUE(parseCustomContentType("application/vnd.kiotty+json;version=2"));
    EXPECT_TRUE(parseCustomContentType("text/x; title=\"a \\\"quoted\\\" word\""));
}

TEST(HttpCustomContentType, MalformedNamesAreInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("/plain"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/plain "));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/plain; charset"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/plain; a=\"open"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/plain; a="));
}

TEST(HttpCustomContentType, HeaderInjectionIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/plain\r\nX-Evil: 1"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, customCodeOf("text/plain; a=\"x\r\ny\""));
}
