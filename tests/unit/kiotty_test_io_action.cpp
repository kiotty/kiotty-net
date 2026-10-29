#include "base/event/kiotty_io_action.h"
#include "base/event/kiotty_event.h"

#include <gtest/gtest.h>

#include <cstring>

using kiotty::net::IoAction;
using kiotty::net::IoActionKind;
using kiotty::net::IoCompletion;
using kiotty::net::IoPurpose;

namespace
{
    KiottyNetConnectionAction publicAction(KiottyNetActionType type, uint32_t timeout_ms)
    {
        KiottyNetConnectionAction action;
        std::memset(&action, 0, sizeof(action));
        action.type = type;
        action.timeout_ms = timeout_ms;
        return action;
    }

    KiottyNetSpan spanOf(const char* text, size_t length)
    {
        KiottyNetSpan span = { text, length };
        return span;
    }
}

static_assert(sizeof(IoAction) < 64, "IoAction travels by value inside every IoCompletion");

TEST(IoAction, FromMapsEveryPublicActionTypeWithPurposeUser)
{
    KiottyNetConnectionAction hold = publicAction(KIOTTY_ACTION_HOLD, 11);
    IoAction held = IoAction::from(hold);
    EXPECT_EQ(held.kind, IoActionKind::HOLD);
    EXPECT_EQ(held.purpose, IoPurpose::USER);
    EXPECT_EQ(held.timeout_ms, 11u);

    KiottyNetConnectionAction read = publicAction(KIOTTY_ACTION_READ, 22);
    read.as.read.exact = 512;
    IoAction reading = IoAction::from(read);
    EXPECT_EQ(reading.kind, IoActionKind::READ);
    EXPECT_EQ(reading.purpose, IoPurpose::USER);
    EXPECT_EQ(reading.timeout_ms, 22u);
    EXPECT_EQ(reading.as.read.exact, 512u);

    KiottyNetConnectionAction read_until = publicAction(KIOTTY_ACTION_READ_UNTIL, 33);
    read_until.as.read_until.delimiter = spanOf("\r\n", 2);
    read_until.as.read_until.max = 1024;
    IoAction scanning = IoAction::from(read_until);
    EXPECT_EQ(scanning.kind, IoActionKind::READ_UNTIL);
    EXPECT_EQ(scanning.purpose, IoPurpose::USER);
    EXPECT_EQ(scanning.timeout_ms, 33u);
    EXPECT_EQ(scanning.as.read_until.max, 1024u);

    KiottyNetConnectionAction write = publicAction(KIOTTY_ACTION_WRITE, 44);
    IoAction writing = IoAction::from(write);
    EXPECT_EQ(writing.kind, IoActionKind::WRITE);
    EXPECT_EQ(writing.purpose, IoPurpose::USER);
    EXPECT_EQ(writing.timeout_ms, 44u);

    KiottyNetConnectionAction work = publicAction(KIOTTY_ACTION_START_WORK, 55);
    IoAction working = IoAction::from(work);
    EXPECT_EQ(working.kind, IoActionKind::START_WORK);
    EXPECT_EQ(working.purpose, IoPurpose::USER);
    EXPECT_EQ(working.timeout_ms, 0u);

    KiottyNetConnectionAction close = publicAction(KIOTTY_ACTION_CLOSE, 66);
    close.as.close.reason = KIOTTY_PROTOCOL_ERROR;
    IoAction closing = IoAction::from(close);
    EXPECT_EQ(closing.kind, IoActionKind::CLOSE);
    EXPECT_EQ(closing.purpose, IoPurpose::USER);
    EXPECT_EQ(closing.timeout_ms, 0u);
    EXPECT_EQ(closing.as.close.reason, KIOTTY_PROTOCOL_ERROR);
}

TEST(IoAction, FromWriteCarriesNoPointerToUserData)
{
    char user_data[] = "payload";
    KiottyNetConnectionAction write = publicAction(KIOTTY_ACTION_WRITE, 0);
    write.as.write.data = spanOf(user_data, sizeof(user_data));
    IoAction writing = IoAction::from(write);
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&writing.as);
    for (size_t i = 0; i + sizeof(char*) <= sizeof(writing.as); ++i)
    {
        const char* candidate = nullptr;
        std::memcpy(&candidate, bytes + i, sizeof(candidate));
        EXPECT_NE(candidate, static_cast<const char*>(user_data)) << i;
    }
}

TEST(IoAction, FromReadUntilCopiesDelimiterOfOneToFourBytesAndZeroScanned)
{
    const char delimiter[] = "\r\n\r\n";
    for (size_t length = 1; length <= IoAction::MAX_DELIMITER_LENGTH; ++length)
    {
        KiottyNetConnectionAction read_until = publicAction(KIOTTY_ACTION_READ_UNTIL, 0);
        read_until.as.read_until.delimiter = spanOf(delimiter, length);
        read_until.as.read_until.max = 64;
        IoAction scanning = IoAction::from(read_until);
        EXPECT_EQ(scanning.as.read_until.length, length);
        EXPECT_EQ(std::memcmp(scanning.as.read_until.delimiter, delimiter, length), 0) << length;
        EXPECT_EQ(scanning.as.read_until.scanned, 0u);
    }
    KiottyNetConnectionAction too_long = publicAction(KIOTTY_ACTION_READ_UNTIL, 0);
    too_long.as.read_until.delimiter = spanOf("12345", 5);
    EXPECT_EQ(IoAction::from(too_long).as.read_until.length, 0u);
    KiottyNetConnectionAction empty = publicAction(KIOTTY_ACTION_READ_UNTIL, 0);
    empty.as.read_until.delimiter = spanOf(nullptr, 2);
    EXPECT_EQ(IoAction::from(empty).as.read_until.length, 0u);
}

TEST(IoAction, SizeOfStaysUnderSixtyFourBytes)
{
    EXPECT_LT(sizeof(IoAction), 64u);
    EXPECT_LE(sizeof(IoCompletion), 64u);
}
