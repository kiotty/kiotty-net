#include "http/data/repository/kiotty_http_shared_headers.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

using kiotty::net::implementation::HttpSharedHeaders;
using kiotty::net::implementation::Span;

namespace
{
    constexpr size_t TEXT_BYTES = 32;
    constexpr size_t MAX_ENTRIES = 3;

    Span<const char> spanOf(const std::string& text)
    {
        return Span<const char>{ text.data(), text.size() };
    }

    std::string textOf(KiottyNetSpan span)
    {
        return std::string(span.data, span.length);
    }

    struct SharedFixture
    {
        std::array<char, TEXT_BYTES>                 text{};
        std::array<KiottyNetHttpHeader, MAX_ENTRIES> entries{};
        HttpSharedHeaders shared{ Span<char>{ text.data(), text.size() },
                                  Span<KiottyNetHttpHeader>{ entries.data(), entries.size() } };

        KiottyNetCode set(const std::string& name, const std::string& value)
        {
            return shared.set(spanOf(name), spanOf(value));
        }

        KiottyNetCode remove(const std::string& name)
        {
            return shared.remove(spanOf(name));
        }

        std::vector<std::string> lines() const
        {
            std::vector<std::string> found;
            for (const KiottyNetHttpHeader& header : shared.list())
            {
                found.push_back(textOf(header.name) + "=" + textOf(header.value));
            }
            return found;
        }

        std::string usedText(size_t bytes) const
        {
            return std::string(text.data(), bytes);
        }
    };

    using Lines = std::vector<std::string>;
}

TEST(HttpSharedHeaders, StartsEmpty)
{
    SharedFixture fixture;
    EXPECT_EQ(0u, fixture.shared.list().length);
}

TEST(HttpSharedHeaders, SetAddsTrimmedValues)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("Authorization", " \tBearer a\t "));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("UA", "d/1"));
    EXPECT_EQ((Lines{ "Authorization=Bearer a", "UA=d/1" }), fixture.lines());
}

TEST(HttpSharedHeaders, SameNameIgnoringCaseIsReplaced)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("Token", "old"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("UA", "d/1"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("token", "newer"));
    EXPECT_EQ((Lines{ "UA=d/1", "token=newer" }), fixture.lines());
}

TEST(HttpSharedHeaders, RemoveCompactsAndKeepsTheOtherEntries)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "1111"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("Bb", "22"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("Ccc", "3"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.remove("bB"));
    EXPECT_EQ((Lines{ "A=1111", "Ccc=3" }), fixture.lines());
    EXPECT_EQ("A1111Ccc3", fixture.usedText(9));
    EXPECT_EQ(std::string(4, '\0'), std::string(fixture.text.data() + 9, 4));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.remove("A"));
    EXPECT_EQ((Lines{ "Ccc=3" }), fixture.lines());
    EXPECT_EQ("Ccc3", fixture.usedText(4));
}

TEST(HttpSharedHeaders, RemovingAnAbsentNameIsSuccess)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "1"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.remove("B"));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.remove(""));
    EXPECT_EQ((Lines{ "A=1" }), fixture.lines());
}

TEST(HttpSharedHeaders, ClearRemovesEverythingAndWipesTheText)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("Authorization", "secret"));
    fixture.shared.clear();
    EXPECT_EQ(0u, fixture.shared.list().length);
    EXPECT_EQ(std::string(TEXT_BYTES, '\0'), fixture.usedText(TEXT_BYTES));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "1"));
    EXPECT_EQ((Lines{ "A=1" }), fixture.lines());
}

TEST(HttpSharedHeaders, TextExactlyFullIsAcceptedAndOneMoreByteIsBufferFullUnchanged)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", std::string(TEXT_BYTES - 1, 'v')));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.set("B", ""));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.set("A", std::string(TEXT_BYTES, 'w')));
    ASSERT_EQ(1u, fixture.shared.list().length);
    EXPECT_EQ("A=" + std::string(TEXT_BYTES - 1, 'v'), fixture.lines()[0]);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("a", std::string(TEXT_BYTES - 1, 'w')));
    EXPECT_EQ("a=" + std::string(TEXT_BYTES - 1, 'w'), fixture.lines()[0]);
}

TEST(HttpSharedHeaders, EntryTableFullIsBufferFullUnchangedButReplacingStillWorks)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "1"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("B", "2"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("C", "3"));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, fixture.set("D", "4"));
    EXPECT_EQ((Lines{ "A=1", "B=2", "C=3" }), fixture.lines());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("b", "22"));
    EXPECT_EQ((Lines{ "A=1", "C=3", "b=22" }), fixture.lines());
}

TEST(HttpSharedHeaders, InvalidHeadersAreRejectedUnchanged)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "1"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.set("", "1"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.set("Bad Name", "1"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.set("A", "x\r\nB: 2"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.set("A", std::string("x\0y", 3)));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.set("Host", "h"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.set("connection", "close"));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, fixture.shared.set(spanOf("B"), Span<const char>{ nullptr, 2 }));
    EXPECT_EQ((Lines{ "A=1" }), fixture.lines());
}

TEST(HttpSharedHeaders, EntriesPointIntoTheTextAfterManyChanges)
{
    SharedFixture fixture;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "1"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("B", "22"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("C", "333"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("A", "4444"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.remove("C"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, fixture.set("D", ""));
    EXPECT_EQ((Lines{ "B=22", "A=4444", "D=" }), fixture.lines());
    EXPECT_EQ("B22A4444D", fixture.usedText(9));
    for (const KiottyNetHttpHeader& header : fixture.shared.list())
    {
        EXPECT_GE(header.name.data, fixture.text.data());
        EXPECT_LE(header.value.data + header.value.length, fixture.text.data() + TEXT_BYTES);
    }
}
