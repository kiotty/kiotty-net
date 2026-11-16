#include "http/domain/entity/kiotty_http_status_line.h"

#include <gtest/gtest.h>

#include <string>

using kiotty::net::implementation::HttpVersion;
using kiotty::net::implementation::parseStatusLine;
using kiotty::net::implementation::Span;
using kiotty::net::implementation::StatusLineResult;

namespace
{
    StatusLineResult parse(const std::string& line)
    {
        return parseStatusLine(Span<const char>{ line.data(), line.size() });
    }

    KiottyNetCode codeOf(const std::string& line)
    {
        return parse(line).code;
    }
}

TEST(HttpStatusLine, EmptyInputIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf(""));
}

TEST(HttpStatusLine, StatusWithoutReasonIsAccepted)
{
    const StatusLineResult parsed = parse("HTTP/1.1 200");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(200, parsed.value.status);
    EXPECT_EQ(HttpVersion::V11, parsed.value.version);
}

TEST(HttpStatusLine, ReasonPhraseIsDiscarded)
{
    const StatusLineResult parsed = parse("HTTP/1.1 404 Not Found");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(404, parsed.value.status);
}

TEST(HttpStatusLine, EmptyReasonAfterTheSpaceIsAccepted)
{
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf("HTTP/1.1 204 "));
}

TEST(HttpStatusLine, Http10IsAccepted)
{
    const StatusLineResult parsed = parse("HTTP/1.0 200 OK");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(HttpVersion::V10, parsed.value.version);
}

TEST(HttpStatusLine, LongReasonIsAccepted)
{
    const std::string line = "HTTP/1.1 500 " + std::string(4096, 'r');
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf(line));
}

TEST(HttpStatusLine, ReasonWithTabAndObsTextIsAccepted)
{
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf("HTTP/1.1 200 \tO\xC3\xA9K"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf("HTTP/1.1 200 \xFF\x80"));
}

TEST(HttpStatusLine, InvalidUtf8InTheReasonIsAcceptedAsObsText)
{
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf("HTTP/1.1 200 \xC3\x28"));
}

TEST(HttpStatusLine, MiddleNullByteIsProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf(std::string("HTTP/1.1 200 O\0K", 16)));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf(std::string("HTTP/1.1 2\0" "0", 12)));
}

TEST(HttpStatusLine, ControlCharactersInTheReasonAreProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 200 O\rK"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 200 O\nK"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 200 \x7F"));
}

TEST(HttpStatusLine, StatusBoundsAreOneHundredToFiveNinetyNine)
{
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf("HTTP/1.1 100"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOf("HTTP/1.1 599"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 099"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 600"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 000"));
}

TEST(HttpStatusLine, StatusMustBeExactlyThreeDigits)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 20"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 2000"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 20 OK"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 +20"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 2a0"));
}

TEST(HttpStatusLine, MalformedVersionsAndSeparatorsAreProtocolError)
{
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/2.0 200"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("http/1.1 200"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1  200"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1\t200"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 200\tOK"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf(" HTTP/1.1 200"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOf("HTTP/1.1 200\r"));
}

TEST(HttpVersionText, OnlyTheTwoExactVersionsAreAccepted)
{
    using kiotty::net::implementation::parseHttpVersion;
    const auto codeOfVersion = [](const std::string& text) {
        return parseHttpVersion(Span<const char>{ text.data(), text.size() }).code;
    };
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOfVersion(""));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOfVersion("HTTP/1.1"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, codeOfVersion("HTTP/1.0"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOfVersion("HTTP/1.11"));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOfVersion(std::string("HTTP/1\0.1", 9)));
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, codeOfVersion("HTTP/1.\xC3"));
}
