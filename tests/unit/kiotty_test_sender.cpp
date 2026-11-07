#include "data/repository/connection/kiotty_sender.h"
#include "core/kiotty_stage.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using kiotty::net::implementation::Stage;
using kiotty::net::implementation::StageBase;
using kiotty::net::implementation::Sender;

namespace
{
    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan result = { text.data(), text.size() };
        return result;
    }

    std::string readableOf(StageBase& stage)
    {
        StageBase::Consumer out = stage.consumer();
        return std::string(out.peek(), out.readable());
    }
}

TEST(Sender, StageCopiesDataAndReadableEqualsItsLength)
{
    Stage<16> stage;
    std::string data = "hello";
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf(data)));
    data.assign(data.size(), 'x');
    EXPECT_EQ(5u, stage.consumer().readable());
    EXPECT_EQ("hello", readableOf(stage));
}

TEST(Sender, StageBeyondWritableReturnsBufferFullAndWritesNothing)
{
    Stage<8> stage;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf("abc")));
    std::string beyond(stage.provider().writable() + 1, 'z');
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, Sender::stage(stage, spanOf(beyond)));
    EXPECT_EQ("abc", readableOf(stage));
}

TEST(Sender, StageOfExactlyWritableSucceeds)
{
    Stage<8> stage;
    std::string exact(8, 'a');
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf(exact)));
    EXPECT_EQ(0u, stage.provider().writable());
    EXPECT_EQ(exact, readableOf(stage));
}

TEST(Sender, ReleaseConsumesAndAlwaysCompacts)
{
    Stage<16> stage;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf("0123456789")));
    Sender::release(stage, 4);
    EXPECT_EQ("456789", readableOf(stage));
    EXPECT_EQ(stage.capacity() - 6, stage.provider().writable());
}

TEST(Sender, ReleaseOfThePeekedLengthKeepsWhatWasStagedBehind)
{
    Stage<32> stage;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf("first")));
    size_t picked = stage.consumer().readable();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf("second")));
    Sender::release(stage, picked);
    EXPECT_EQ("second", readableOf(stage));
}

TEST(Sender, ContinuousAppendWhileReleasingNeverLocksIntoBufferFull)
{
    Stage<16> stage;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf("abcd")));
    for (int i = 0; i < 1000; ++i)
    {
        size_t picked = stage.consumer().readable();
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(stage, spanOf("efgh")));
        Sender::release(stage, picked);
        ASSERT_EQ(4u, stage.consumer().readable());
        ASSERT_EQ(12u, stage.provider().writable());
    }
}

TEST(Sender, StageAndReleaseAreStaticAndTakeAnyStageBase)
{
    Stage<16> send;
    Stage<16> notify;
    StageBase& first = send;
    StageBase& second = notify;
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(first, spanOf("user")));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(second, spanOf("push")));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, Sender::stage(first, spanOf("!")));
    Sender::release(second, 2);
    EXPECT_EQ("user!", readableOf(send));
    EXPECT_EQ("sh", readableOf(notify));
    notify.clear();
    EXPECT_EQ(0u, notify.consumer().readable());
    EXPECT_EQ("user!", readableOf(send));
}

TEST(Sender, StageOfNullDataWithLengthIsInvalidArgument)
{
    Stage<16> stage;
    KiottyNetSpan missing = { nullptr, 4 };
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, Sender::stage(stage, missing));
    EXPECT_EQ(0u, stage.consumer().readable());
}
