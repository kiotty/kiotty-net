#include "data/repository/connection/kiotty_connection.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_deadline.h"
#include "data/repository/connection/kiotty_caller.h"
#include "domain/entity/kiotty_handler.h"
#include "support/kiotty_test_completion.h"
#include "support/kiotty_test_handler.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace net = kiotty::net::implementation;

using kiotty_test::Call;
using kiotty_test::ConnectionBench;
using kiotty_test::limitsOf;
using kiotty_test::isWorking;
using kiotty_test::readEventOf;
using kiotty_test::spanOf;
using kiotty_test::thenClose;
using kiotty_test::thenHold;
using kiotty_test::thenRead;
using kiotty_test::thenReadUntilSpan;
using kiotty_test::thenWork;
using kiotty_test::thenWriteSpan;
using kiotty_test::writeEventOf;
namespace completion = kiotty_test::completion;

namespace
{
    typedef ConnectionBench<1> Bench;

    const char* const BUSY_MESSAGE = "action rejected: io outstanding. use notify";

    net::ConnectionLimits smallLimits()
    {
        return limitsOf(64, 64, 64);
    }

    std::string payloadOf(net::Connection& connection, const net::IoAction& action)
    {
        net::IoPayload payload = connection.resolve(action);
        return std::string(static_cast<const char*>(payload.buffer), payload.length);
    }

    class StateProbe final : public net::Handler
    {
    public:
        StateProbe(KiottyNetEndpoint& owner, int32_t initial)
            : net::Handler(owner)
            , on_connected(thenHold(0))
            , on_work_started(thenWork())
            , _initial(initial)
        {
        }

        KiottyNetCode validate() const override
        {
            return KIOTTY_NET_CODE_SUCCESS;
        }

        int32_t initialState() const override
        {
            return _initial;
        }

        KiottyNetConnectionDecision admit(const KiottyNetConnectionInfo&) const override
        {
            return KiottyNet_ignoreConnection();
        }

        KiottyNetConnectionAction dispatch(const net::Caller& caller, const KiottyNetConnectionEvent& event) const override
        {
            states.push_back(caller.machine_state);
            types.push_back(event.type);
            caller.machine_state = caller.machine_state + 1;
            switch (event.type)
            {
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED:
                return on_connected;
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED:
                return on_work_started;
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_READ:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT:
            case KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED:
                break;
            }
            return thenHold(0);
        }

        void report(const KiottyNetConnectionInfo&, KiottyNetCode code, const char*) const override
        {
            reported.push_back(code);
        }

        KiottyNetConnectionAction                         on_connected;
        KiottyNetConnectionAction                         on_work_started;
        mutable std::vector<int32_t>                      states;
        mutable std::vector<KiottyNetConnectionEventType> types;
        mutable std::vector<KiottyNetCode>                reported;

    private:
        int32_t _initial;
    };

    void startWork(Bench& bench, net::Connection& connection)
    {
        bench.recorder.then(thenWork());
        net::IoAction requested = bench.opened(connection);
        ASSERT_EQ(net::IoActionKind::START_WORK, requested.kind);
        net::IoAction started = bench.complete(connection, completion::workReady());
        ASSERT_EQ(net::IoActionKind::NONE, started.kind);
        ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.verify(connection.state().id.generation));
    }

    void expectDeadlineAround(const net::Connection& connection, uint64_t before, uint64_t after, uint32_t timeout_milliseconds)
    {
        EXPECT_GE(connection.state().deadline, before + timeout_milliseconds);
        EXPECT_LE(connection.state().deadline, after + timeout_milliseconds);
    }
}

TEST(Connection, FreshConnectionIsIdleNotAliveWithBothEventsEmpty)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.host.at(0);
    EXPECT_EQ(net::Phase::IDLE, connection.state().phase);
    EXPECT_FALSE(connection.state().is_alive);
    EXPECT_EQ(0u, connection.state().deadline);
    EXPECT_FALSE(isWorking(connection));
    EXPECT_FALSE(readEventOf(connection).pending);
    EXPECT_FALSE(writeEventOf(connection).pending);
}

TEST(Connection, InitializeRaisesGenerationAndHandleChangesEachTime)
{
    Bench bench(smallLimits());
    net::Connection& first = bench.open();
    bench.opened(first);
    KiottyNetConnectionHandle before = bench.recorder.last(Call::ON_READY).handle;
    bench.host.release(first);
    net::Connection& second = bench.open();
    bench.opened(second);
    KiottyNetConnectionHandle after = bench.recorder.last(Call::ON_READY).handle;
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(before.owner, after.owner);
    EXPECT_NE(before.id, after.id);
    EXPECT_EQ(kiotty::net::implementation::PoolHandle::fromValue(before.id).index, kiotty::net::implementation::PoolHandle::fromValue(after.id).index);
    EXPECT_NE(kiotty::net::implementation::PoolHandle::fromValue(before.id).generation, kiotty::net::implementation::PoolHandle::fromValue(after.id).generation);
}

TEST(Connection, StreamHandleSharesOwnerAndIdWithTheConnectionHandle)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    KiottyNetConnectionHandle handle = bench.recorder.last(Call::ON_READY).handle;
    KiottyNetConnectionStream stream = bench.recorder.last(Call::ON_WORK_STARTED).stream;
    EXPECT_EQ(handle.owner, stream.owner);
    EXPECT_EQ(handle.id, stream.id);
    EXPECT_EQ(&bench.owner, stream.owner);
    EXPECT_EQ(connection.state().id.value(), stream.id);
}

TEST(Connection, EventForChoosesByDirection)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    const net::IoEvent* write = &writeEventOf(connection);
    const net::IoEvent* read = &readEventOf(connection);
    EXPECT_NE(write, read);
    EXPECT_EQ(write, &connection.eventFor(net::IoAction::write(net::IoPurpose::USER, 0, 1)));
    EXPECT_EQ(write, &connection.eventFor(net::IoAction::write(net::IoPurpose::NOTIFY, 0, 1)));
    EXPECT_EQ(write, &connection.eventFor(net::IoAction::write(net::IoPurpose::HANDSHAKE, 0, 0)));
    EXPECT_EQ(write, &connection.eventFor(net::IoAction::write(net::IoPurpose::CONTROL, 0, 0)));
    EXPECT_EQ(read, &connection.eventFor(net::IoAction::read(net::IoPurpose::USER, 0, 4)));
    EXPECT_EQ(read, &connection.eventFor(net::IoAction::readUntil(0, spanOf("\n"), 8)));
}

TEST(Connection, OnOpenedPlainCallsOnReadyOnceAndAppliesItsAction)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 500));
    uint64_t before = kiotty::net::implementation::monotonicMilliseconds();
    net::IoAction action = bench.opened(connection);
    uint64_t after = kiotty::net::implementation::monotonicMilliseconds();
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_READY));
    EXPECT_EQ(1u, bench.recorder.total());
    EXPECT_EQ(net::IoActionKind::READ, action.kind);
    EXPECT_EQ(4u, action.as.read.exact);
    EXPECT_EQ(500u, action.timeout_milliseconds);
    expectDeadlineAround(connection, before, after, 500);
    EXPECT_FALSE(connection.state().isExpired(before + 499));
    EXPECT_TRUE(connection.state().isExpired(after + 500));
    bench.complete(connection, completion::timeout());
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_TIMEOUT));
    EXPECT_EQ(0u, connection.state().deadline);
    EXPECT_FALSE(connection.state().isExpired(after + 500));
}

namespace
{
    class ConnectionCompletionKind : public ::testing::TestWithParam<int>
    {
    };

    net::IoCompletion closingCompletion(int which)
    {
        switch (which)
        {
        case 0:
            return completion::receive(net::IoAction::read(net::IoPurpose::USER, 0, 1), 1);
        case 1:
            return completion::sent(net::IoAction::write(net::IoPurpose::USER, 0, 1), 1);
        case 2:
            return completion::timeout();
        case 3:
            return completion::workReady();
        case 4:
            return completion::workDone(net::IoAction::read(net::IoPurpose::USER, 0, 1));
        default:
            return completion::receive(net::IoAction::read(net::IoPurpose::USER, 0, 1), 0, net::SocketCode::CANCELLED);
        }
    }
}

TEST_P(ConnectionCompletionKind, AnyCompletionInClosingReturnsNoneAndCallsNothing)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    bench.recorder.then(thenClose(KIOTTY_NET_CODE_SUCCESS));
    net::IoAction closed = bench.complete(connection, completion::timeout());
    ASSERT_EQ(net::IoActionKind::CLOSE, closed.kind);
    ASSERT_EQ(net::Phase::CLOSING, connection.state().phase);
    bench.recorder.clear();
    net::IoAction late = bench.complete(connection, closingCompletion(GetParam()));
    EXPECT_EQ(net::IoActionKind::NONE, late.kind);
    EXPECT_EQ(0u, bench.recorder.total());
    EXPECT_EQ(net::Phase::CLOSING, connection.state().phase);
}

INSTANTIATE_TEST_SUITE_P(EveryKindAndCancelled, ConnectionCompletionKind, ::testing::Range(0, 6));

TEST(Connection, ReceiveWithClosedCodeReturnsClosePeerClosedAndOnDisconnectedOnce)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 500));
    net::IoAction read = bench.opened(connection);
    ASSERT_NE(0u, connection.state().deadline);
    net::IoAction closed = bench.complete(connection, completion::receive(read, 0, net::SocketCode::CLOSED));
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, closed.as.close.reason);
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_DISCONNECTED));
    const kiotty_test::Record& gone = bench.recorder.last(Call::ON_DISCONNECTED);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, gone.code);
    EXPECT_EQ(&bench.recorder, gone.app_context);
    EXPECT_EQ(&bench.recorder, gone.connection_context);
    EXPECT_EQ(connection.state().id.value(), gone.handle.id);
    EXPECT_EQ(net::Phase::CLOSING, connection.state().phase);
    EXPECT_EQ(0u, connection.state().deadline);
}

TEST(Connection, ReceiveOrSendWithErrorCodeReturnsCloseSocketError)
{
    Bench receiving(smallLimits());
    net::Connection& reader = receiving.open();
    receiving.recorder.then(thenRead(4, 0));
    net::IoAction read = receiving.opened(reader);
    net::IoAction failed = receiving.complete(reader, completion::receive(read, 0, net::SocketCode::RESET));
    EXPECT_EQ(net::IoActionKind::CLOSE, failed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_SOCKET_ERROR, failed.as.close.reason);
    EXPECT_EQ(KIOTTY_NET_CODE_SOCKET_ERROR, receiving.recorder.last(Call::ON_DISCONNECTED).code);

    Bench sending(smallLimits());
    net::Connection& writer = sending.open();
    sending.recorder.thenWrite("data", 0);
    sending.opened(writer);
    net::IoAction write = sending.pick(writer);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    net::IoAction refused = sending.complete(writer, completion::sent(write, 0, net::SocketCode::RESET));
    EXPECT_EQ(net::IoActionKind::CLOSE, refused.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_SOCKET_ERROR, refused.as.close.reason);
    EXPECT_EQ(0u, sending.recorder.count(Call::ON_WRITTEN));
}

TEST(Connection, CloseIsAppliedOnlyOnceEvenIfReadAndWriteCompletionsBothCarryErrors)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 0));
    net::IoAction read = bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push"));
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    net::IoAction first = bench.complete(connection, completion::receive(read, 0, net::SocketCode::RESET));
    net::IoAction second = bench.complete(connection, completion::sent(write, 0, net::SocketCode::RESET));
    EXPECT_EQ(net::IoActionKind::CLOSE, first.kind);
    EXPECT_EQ(net::IoActionKind::NONE, second.kind);
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_DISCONNECTED));
}

TEST(Connection, ReceiveThatSatisfiesExactReadCallsOnReadWithExactlyThoseBytes)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 0));
    bench.recorder.then(thenRead(1, 0));
    net::IoAction read = bench.opened(connection);
    net::IoAction next = bench.deliver(connection, read, "abcd");
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("abcd", bench.recorder.last(Call::ON_READ).data);
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(1u, next.as.read.exact);
}

TEST(Connection, ReceiveShortOfExactReturnsTheSameReadAndNoCallback)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 700));
    net::IoAction read = bench.opened(connection);
    net::IoAction next = bench.deliver(connection, read, "abc");
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(4u, next.as.read.exact);
    EXPECT_EQ(700u, next.timeout_milliseconds);
}

TEST(Connection, ReceiveWithMoreThanExactDeliversExactAndKeepsTheRest)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 0));
    net::IoAction read = bench.opened(connection);
    net::IoAction held = bench.deliver(connection, read, "abcdefghij");
    EXPECT_EQ(net::IoActionKind::HOLD, held.kind);
    EXPECT_EQ("abcd", bench.recorder.last(Call::ON_READ).data);
    bench.recorder.then(thenRead(6, 0));
    bench.complete(connection, completion::timeout());
    EXPECT_EQ(2u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("efghij", bench.recorder.last(Call::ON_READ).data);
}

TEST(Connection, TwoMessagesAlreadyBufferedAreDeliveredInOneDriveWhenHandlerKeepsReading)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 0));
    bench.recorder.then(thenRead(4, 0));
    bench.recorder.then(thenRead(4, 0));
    net::IoAction read = bench.opened(connection);
    net::IoAction next = bench.deliver(connection, read, "abcdefgh");
    ASSERT_EQ(2u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("abcd", bench.recorder.at(1).data);
    EXPECT_EQ("efgh", bench.recorder.at(2).data);
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(4u, next.as.read.exact);
}

TEST(Connection, DriveTerminatesWithinRecvCapacityIterations)
{
    const size_t capacity = 64;
    Bench bench(limitsOf(capacity, 64, 64));
    net::Connection& connection = bench.open();
    for (size_t i = 0; i <= capacity; ++i)
    {
        bench.recorder.then(thenRead(1, 0));
    }
    net::IoAction read = bench.opened(connection);
    net::IoAction next = bench.deliver(connection, read, std::string(capacity, 'x'));
    EXPECT_EQ(capacity, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(1u, next.as.read.exact);
}

TEST(Connection, RefillCompactsSoTheReturnedReadHasWritableRoom)
{
    Bench bench(limitsOf(16, 64, 64));
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 0));
    bench.recorder.then(thenRead(4, 0));
    net::IoAction read = bench.opened(connection);
    net::IoAction next = bench.deliver(connection, read, "abcdef");
    ASSERT_EQ(net::IoActionKind::READ, next.kind);
    net::IoPayload room = connection.resolve(next);
    EXPECT_NE(nullptr, room.buffer);
    EXPECT_EQ(16u - 2u, room.length);
}

TEST(Connection, FullStageWithoutTheConditionReturnsCloseRequestTooLarge)
{
    Bench bench(limitsOf(16, 64, 64));
    net::Connection& connection = bench.open();
    bench.recorder.thenReadUntil("\r\n", 64, 0);
    net::IoAction read = bench.opened(connection);
    net::IoAction closed = bench.deliver(connection, read, std::string(16, 'a'));
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, closed.as.close.reason);
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_ERROR));
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, bench.recorder.last(Call::ON_ERROR).code);
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, bench.recorder.last(Call::ON_DISCONNECTED).code);
}

TEST(Connection, ReadExactLargerThanReceiveCapacityReturnsCloseRequestTooLarge)
{
    Bench bench(limitsOf(4096, 64, 64));
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(8192, 0));
    net::IoAction closed = bench.opened(connection);
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, closed.as.close.reason);
    EXPECT_EQ(KIOTTY_NET_CODE_REQUEST_TOO_LARGE, bench.recorder.last(Call::ON_DISCONNECTED).code);
}

TEST(Connection, ReadUntilDeliversUpToAndIncludingTheDelimiter)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenReadUntil("\r\n", 32, 0);
    bench.recorder.then(thenRead(2, 0));
    net::IoAction read = bench.opened(connection);
    bench.deliver(connection, read, "abc\r\nde");
    ASSERT_EQ(2u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("abc\r\n", bench.recorder.at(1).data);
    EXPECT_EQ("de", bench.recorder.at(2).data);
}

TEST(Connection, ReadUntilWithDelimiterSplitAcrossTwoReceivesStillFinds)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(2, 0));
    bench.recorder.thenReadUntil("\r\n", 32, 0);
    net::IoAction read = bench.opened(connection);
    net::IoAction until = bench.deliver(connection, read, "xyabc\r");
    ASSERT_EQ(net::IoActionKind::READ_UNTIL, until.kind);
    EXPECT_EQ(3u, until.as.read_until.scanned);
    bench.deliver(connection, until, "\n");
    ASSERT_EQ(2u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("abc\r\n", bench.recorder.last(Call::ON_READ).data);
}

TEST(Connection, ReadUntilDoesNotRescanBytesItAlreadyScanned)
{
    const size_t chunk = 1024;
    Bench bench(limitsOf(4096, 64, 64));
    net::Connection& connection = bench.open();
    bench.recorder.thenReadUntil("\r\n", 8192, 0);
    net::IoAction until = bench.opened(connection);
    for (size_t i = 1; i <= 3; ++i)
    {
        until = bench.deliver(connection, until, std::string(chunk, 'a'));
        ASSERT_EQ(net::IoActionKind::READ_UNTIL, until.kind);
        EXPECT_EQ(i * chunk - 1, until.as.read_until.scanned);
    }
}

TEST(Connection, ReadUntilWithoutDelimiterWithinMaxReturnsCloseProtocolError)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenReadUntil("\r\n", 8, 0);
    net::IoAction read = bench.opened(connection);
    net::IoAction closed = bench.deliver(connection, read, "abcdefghi");
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, closed.as.close.reason);
    EXPECT_EQ(KIOTTY_NET_CODE_PROTOCOL_ERROR, bench.recorder.last(Call::ON_DISCONNECTED).code);
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_READ));
}

namespace
{
    void expectRejectedAsInvalidArgument(const KiottyNetConnectionAction& returned)
    {
        Bench bench(smallLimits());
        net::Connection& connection = bench.open();
        bench.recorder.then(returned);
        net::IoAction closed = bench.opened(connection);
        EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, closed.as.close.reason);
        ASSERT_EQ(1u, bench.recorder.count(Call::ON_ERROR));
        const kiotty_test::Record& error = bench.recorder.last(Call::ON_ERROR);
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, error.code);
        ASSERT_NE(nullptr, error.message);
        EXPECT_NE(0u, std::strlen(error.message));
        EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, bench.recorder.last(Call::ON_DISCONNECTED).code);
        EXPECT_EQ(net::Phase::CLOSING, connection.state().phase);
    }
}

TEST(Connection, ReadExactZeroIsRejectedAsInvalidArgument)
{
    expectRejectedAsInvalidArgument(thenRead(0, 0));
}

TEST(Connection, ReadUntilWithEmptyOrFiveByteDelimiterIsRejected)
{
    std::string five = "12345";
    KiottyNetSpan empty = { five.data(), 0 };
    expectRejectedAsInvalidArgument(thenReadUntilSpan(empty, 16, 0));
    expectRejectedAsInvalidArgument(thenReadUntilSpan(spanOf(five), 16, 0));
}

TEST(Connection, OnReadReturningWriteStagesIntoSendAndReturnsHoldWithDeadline)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(1, 0));
    bench.recorder.thenWrite("pong", 300);
    net::IoAction read = bench.opened(connection);
    uint64_t before = kiotty::net::implementation::monotonicMilliseconds();
    net::IoAction held = bench.deliver(connection, read, "x");
    uint64_t after = kiotty::net::implementation::monotonicMilliseconds();
    EXPECT_EQ(net::IoActionKind::HOLD, held.kind);
    EXPECT_EQ(300u, held.timeout_milliseconds);
    expectDeadlineAround(connection, before, after, 300);
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::USER, write.purpose);
    EXPECT_EQ(4u, write.as.write.length);
    EXPECT_EQ("pong", payloadOf(connection, write));
}

TEST(Connection, WriteLargerThanSendCapacityReturnsCloseBufferFullWithoutPartialCopy)
{
    Bench bench(limitsOf(64, 8, 64));
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(1, 0));
    bench.recorder.thenWrite(std::string(9, 'w'), 0);
    net::IoAction read = bench.opened(connection);
    net::IoAction closed = bench.deliver(connection, read, "x");
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, closed.as.close.reason);
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, bench.recorder.last(Call::ON_DISCONNECTED).code);
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_ERROR));
    bench.host.release(connection);
    net::Connection& reopened = bench.open();
    bench.opened(reopened);
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(reopened).kind);
}

TEST(Connection, WriteOfExactlySendCapacitySucceeds)
{
    Bench bench(limitsOf(64, 8, 64));
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite(std::string(8, 'w'), 0);
    net::IoAction held = bench.opened(connection);
    EXPECT_EQ(net::IoActionKind::HOLD, held.kind);
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(8u, write.as.write.length);
    EXPECT_EQ(std::string(8, 'w'), payloadOf(connection, write));
}

TEST(Connection, WriteOfZeroBytesFollowsTheConfirmedContract)
{
    std::string empty;
    expectRejectedAsInvalidArgument(thenWriteSpan(spanOf(empty), 0));
}

TEST(Connection, WriteWithNullDataAndNonZeroLengthIsRejectedWithInvalidArgument)
{
    KiottyNetSpan missing = { nullptr, 10 };
    expectRejectedAsInvalidArgument(thenWriteSpan(missing, 0));
}

TEST(Connection, HandlerReturningUnknownActionTypeIsRejectedWithInvalidArgument)
{
    KiottyNetConnectionAction unknown = thenHold(0);
    unknown.type = static_cast<KiottyNetActionType>(99);
    expectRejectedAsInvalidArgument(unknown);
}

TEST(Connection, PickWriteOnIdleOrClosingIsNone)
{
    Bench bench(smallLimits());
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(bench.host.at(0)).kind);
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite("user", 100);
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push"));
    bench.recorder.then(thenClose(KIOTTY_NET_CODE_SUCCESS));
    bench.complete(connection, completion::timeout());
    ASSERT_EQ(net::Phase::CLOSING, connection.state().phase);
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
}

TEST(Connection, PickWriteWhileWorkIsRequestedWorkingOrDoneIsNone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenWork());
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push"));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    bench.complete(connection, completion::workReady());
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenHold(0)));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    bench.complete(connection, completion::workDone(connection.flows().work.takeOutcome()));
    net::IoAction write = bench.pick(connection);
    EXPECT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::NOTIFY, write.purpose);
}

TEST(Connection, PickWriteWithPendingWriteEventIsNone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push"));
    writeEventOf(connection).pending = true;
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    writeEventOf(connection).pending = false;
    EXPECT_EQ(net::IoActionKind::WRITE, bench.pick(connection).kind);
}

TEST(Connection, PickWriteWithNotifyAndSendBothStagedPicksNotifyFirst)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite("response", 0);
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "N1"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "N2"));
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::NOTIFY, write.purpose);
    EXPECT_EQ(4u, write.as.write.length);
    EXPECT_EQ("N1N2", payloadOf(connection, write));
}

TEST(Connection, PickWriteWithOnlySendStagedReturnsWriteUserOfItsWholeReadable)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite("response", 0);
    bench.opened(connection);
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::USER, write.purpose);
    EXPECT_EQ(8u, write.as.write.length);
    EXPECT_EQ("response", payloadOf(connection, write));
}

TEST(Connection, PickWriteWithNothingStagedIsNone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
}

TEST(Connection, ResolveWriteOfNotifyKeepsThePickedLengthWhenMoreIsAppendedBeforeResolve)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "abc"));
    net::IoAction write = bench.pick(connection);
    net::IoPayload picked = connection.resolve(write);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "def"));
    net::IoPayload resolved = connection.resolve(write);
    EXPECT_EQ(picked.buffer, resolved.buffer);
    EXPECT_EQ(3u, resolved.length);
    EXPECT_EQ("abc", payloadOf(connection, write));
}

TEST(Connection, ResolveReadPointsAtReceiveStageFreeSpaceOfWritableLength)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(4, 0));
    net::IoAction read = bench.opened(connection);
    net::IoPayload empty = connection.resolve(read);
    EXPECT_NE(nullptr, empty.buffer);
    EXPECT_EQ(64u, empty.length);
    net::IoAction again = bench.deliver(connection, read, "abc");
    net::IoPayload rest = connection.resolve(again);
    EXPECT_EQ(static_cast<char*>(empty.buffer) + 3, rest.buffer);
    EXPECT_EQ(61u, rest.length);
}

TEST(Connection, ResolveOfNoneHoldStartWorkCloseIsEmptyPayload)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    net::IoAction actions[] = { net::IoAction::none(), net::IoAction::hold(100), net::IoAction::startWork(), net::IoAction::close(KIOTTY_NET_CODE_SUCCESS) };
    for (const net::IoAction& action : actions)
    {
        net::IoPayload payload = connection.resolve(action);
        EXPECT_EQ(nullptr, payload.buffer);
        EXPECT_EQ(0u, payload.length);
    }
}

TEST(Connection, SendNotifyReleasesOnlyNotifyAndReturnsNone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite("user", 0);
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push!"));
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoPurpose::NOTIFY, write.purpose);
    net::IoAction after = bench.complete(connection, completion::sent(write, 5));
    EXPECT_EQ(net::IoActionKind::NONE, after.kind);
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_WRITTEN));
    net::IoAction next = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, next.kind);
    EXPECT_EQ(net::IoPurpose::USER, next.purpose);
    EXPECT_EQ("user", payloadOf(connection, next));
}

TEST(Connection, SendUserReleasesOnlySendAndCallsOnWrittenWithPlainLength)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite("user", 0);
    bench.recorder.then(thenRead(2, 0));
    bench.opened(connection);
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoPurpose::USER, write.purpose);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push"));
    net::IoAction after = bench.complete(connection, completion::sent(write, 4));
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_WRITTEN));
    EXPECT_EQ(4u, bench.recorder.last(Call::ON_WRITTEN).length);
    EXPECT_EQ(net::IoActionKind::READ, after.kind);
    EXPECT_EQ(2u, after.as.read.exact);
    net::IoAction next = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, next.kind);
    EXPECT_EQ(net::IoPurpose::NOTIFY, next.purpose);
    EXPECT_EQ("push", payloadOf(connection, next));
    bench.complete(connection, completion::sent(next, 4));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
}

TEST(Connection, NotifyStagedWhileNotifyWriteIsInFlightGoesOutAsTheNextWrite)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "abc"));
    net::IoAction first = bench.pick(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "de"));
    bench.complete(connection, completion::sent(first, 3));
    net::IoAction second = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, second.kind);
    EXPECT_EQ(net::IoPurpose::NOTIFY, second.purpose);
    EXPECT_EQ(2u, second.as.write.length);
    EXPECT_EQ("de", payloadOf(connection, second));
}

TEST(Connection, ThenWriteDuringNotifySendOnlyStagesAndSetsDeadline)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(1, 0));
    bench.recorder.thenWrite("resp", 300);
    net::IoAction read = bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "nn"));
    net::IoAction push = bench.pick(connection);
    writeEventOf(connection).pending = true;
    net::IoAction held = bench.deliver(connection, read, "x");
    EXPECT_EQ(net::IoActionKind::HOLD, held.kind);
    EXPECT_NE(0u, connection.state().deadline);
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    writeEventOf(connection).pending = false;
    EXPECT_EQ(net::IoActionKind::NONE, bench.complete(connection, completion::sent(push, 2)).kind);
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::USER, write.purpose);
    EXPECT_EQ("resp", payloadOf(connection, write));
}

TEST(Connection, TimeoutAfterHoldAppliesWhateverHandlerReturns)
{
    Bench writing(smallLimits());
    net::Connection& writer = writing.open();
    writing.recorder.then(thenHold(100));
    writing.recorder.thenWrite("late", 50);
    writing.opened(writer);
    net::IoAction held = writing.complete(writer, completion::timeout());
    EXPECT_EQ(1u, writing.recorder.count(Call::ON_TIMEOUT));
    EXPECT_EQ(net::IoActionKind::HOLD, held.kind);
    EXPECT_EQ(50u, held.timeout_milliseconds);
    EXPECT_EQ("late", payloadOf(writer, writing.pick(writer)));

    Bench working(smallLimits());
    net::Connection& worker = working.open();
    working.recorder.then(thenHold(100));
    working.recorder.then(thenWork());
    working.opened(worker);
    net::IoAction requested = working.complete(worker, completion::timeout());
    EXPECT_EQ(net::IoActionKind::START_WORK, requested.kind);
    EXPECT_TRUE(isWorking(worker));
    EXPECT_TRUE(worker.flows().work.isReady());
}

namespace
{
    enum class Pending
    {
        READ_EVENT,
        STAGED_WRITE
    };

    class ConnectionPendingUserAction : public ::testing::TestWithParam<Pending>
    {
    protected:
        ConnectionPendingUserAction()
            : bench(smallLimits())
            , connection(bench.open())
        {
        }

        void SetUp() override
        {
            if (GetParam() == Pending::READ_EVENT)
            {
                bench.recorder.then(thenRead(4, 100));
                bench.opened(connection);
                readEventOf(connection).pending = true;
                return;
            }
            bench.recorder.thenWrite("data", 100);
            bench.opened(connection);
        }

        void TearDown() override
        {
            readEventOf(connection).pending = false;
        }

        void expectStagesUnchanged()
        {
            readEventOf(connection).pending = false;
            net::IoAction write = bench.pick(connection);
            if (GetParam() == Pending::READ_EVENT)
            {
                EXPECT_EQ(net::IoActionKind::NONE, write.kind);
                return;
            }
            EXPECT_EQ("data", payloadOf(connection, write));
        }

        Bench            bench;
        net::Connection& connection;
    };

    std::string pendingName(const ::testing::TestParamInfo<Pending>& info)
    {
        return info.param == Pending::READ_EVENT ? "ReadEventPending" : "WriteStaged";
    }
}

TEST_P(ConnectionPendingUserAction, TimeoutWithUserActionPendingAndCloseReturnedCloses)
{
    bench.recorder.then(thenClose(KIOTTY_NET_CODE_TIMEOUT));
    net::IoAction closed = bench.complete(connection, completion::timeout());
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, closed.as.close.reason);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, bench.recorder.last(Call::ON_DISCONNECTED).code);
    EXPECT_EQ(net::Phase::CLOSING, connection.state().phase);
}

TEST_P(ConnectionPendingUserAction, TimeoutWithUserActionPendingAndHoldOrReadReturnedOnlyExtendsDeadline)
{
    bench.recorder.then(thenHold(0));
    net::IoAction unlimited = bench.complete(connection, completion::timeout());
    EXPECT_EQ(net::IoActionKind::HOLD, unlimited.kind);
    EXPECT_EQ(0u, connection.state().deadline);

    bench.recorder.then(thenRead(8, 200));
    uint64_t before = kiotty::net::implementation::monotonicMilliseconds();
    net::IoAction extended = bench.complete(connection, completion::timeout());
    uint64_t after = kiotty::net::implementation::monotonicMilliseconds();
    EXPECT_EQ(net::IoActionKind::HOLD, extended.kind);
    expectDeadlineAround(connection, before, after, 200);

    bench.recorder.thenReadUntil("\n", 16, 300);
    before = kiotty::net::implementation::monotonicMilliseconds();
    net::IoAction until = bench.complete(connection, completion::timeout());
    after = kiotty::net::implementation::monotonicMilliseconds();
    EXPECT_EQ(net::IoActionKind::HOLD, until.kind);
    expectDeadlineAround(connection, before, after, 300);

    EXPECT_EQ(0u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_ERROR));
    expectStagesUnchanged();
}

TEST_P(ConnectionPendingUserAction, TimeoutWithUserActionPendingAndWriteReturnedReportsBusyOnceAndExtends)
{
    bench.recorder.thenWrite("x", 300);
    uint64_t before = kiotty::net::implementation::monotonicMilliseconds();
    net::IoAction extended = bench.complete(connection, completion::timeout());
    uint64_t after = kiotty::net::implementation::monotonicMilliseconds();
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_ERROR));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, bench.recorder.last(Call::ON_ERROR).code);
    EXPECT_STREQ(BUSY_MESSAGE, bench.recorder.last(Call::ON_ERROR).message);
    EXPECT_EQ(net::IoActionKind::HOLD, extended.kind);
    expectDeadlineAround(connection, before, after, 300);
    expectStagesUnchanged();
}

TEST_P(ConnectionPendingUserAction, TimeoutWithUserActionPendingAndWorkReturnedReportsBusyAndLeavesNoDeadline)
{
    bench.recorder.then(thenWork());
    net::IoAction extended = bench.complete(connection, completion::timeout());
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_ERROR));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, bench.recorder.last(Call::ON_ERROR).code);
    EXPECT_EQ(net::IoActionKind::HOLD, extended.kind);
    EXPECT_EQ(0u, connection.state().deadline);
    EXPECT_FALSE(isWorking(connection));
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_WORK_STARTED));
}

INSTANTIATE_TEST_SUITE_P(BothShapes, ConnectionPendingUserAction, ::testing::Values(Pending::READ_EVENT, Pending::STAGED_WRITE), pendingName);

TEST(Connection, TimeoutWithOnlyNotifyWriteInFlightIsNotUserActionPending)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenHold(100));
    bench.recorder.then(thenRead(4, 0));
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "n"));
    net::IoAction push = bench.pick(connection);
    ASSERT_EQ(net::IoPurpose::NOTIFY, push.purpose);
    writeEventOf(connection).pending = true;
    net::IoAction read = bench.complete(connection, completion::timeout());
    writeEventOf(connection).pending = false;
    EXPECT_EQ(net::IoActionKind::READ, read.kind);
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_ERROR));
}

TEST(Connection, ApplyStartWorkOnlyMarksRequestedAndClearsDeadline)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(1, 500));
    bench.recorder.then(thenWork());
    net::IoAction read = bench.opened(connection);
    ASSERT_NE(0u, connection.state().deadline);
    net::IoAction requested = bench.deliver(connection, read, "x");
    EXPECT_EQ(net::IoActionKind::START_WORK, requested.kind);
    EXPECT_EQ(0u, connection.state().deadline);
    EXPECT_TRUE(isWorking(connection));
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_WORK_STARTED));
}

TEST(Connection, WorkReadyIsTrueOnlyWhenRequestedAndBothEventsAreEmpty)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenHold(0));
    bench.recorder.then(thenWork());
    bench.opened(connection);
    EXPECT_FALSE(connection.flows().work.isReady());
    bench.complete(connection, completion::timeout());
    readEventOf(connection).pending = true;
    EXPECT_FALSE(connection.flows().work.isReady());
    readEventOf(connection).pending = false;
    writeEventOf(connection).pending = true;
    EXPECT_FALSE(connection.flows().work.isReady());
    writeEventOf(connection).pending = false;
    EXPECT_TRUE(connection.flows().work.isReady());
    bench.complete(connection, completion::workReady());
    EXPECT_FALSE(connection.flows().work.isReady());
}

TEST(Connection, StatePendingFlagsEachHandOffAndIsSettledOnlyWhenNoneIsSet)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenHold(0));
    bench.recorder.then(thenWork());
    bench.opened(connection);
    EXPECT_EQ(net::PENDING_NONE, connection.state().pending);
    EXPECT_TRUE(connection.state().isSettled());
    readEventOf(connection).pending = true;
    EXPECT_EQ(net::PENDING_READING, connection.state().pending);
    writeEventOf(connection).pending = true;
    const uint8_t both = static_cast<uint8_t>(net::PENDING_READING | net::PENDING_WRITING);
    EXPECT_EQ(both, connection.state().pending);
    EXPECT_FALSE(connection.state().isSettled());
    readEventOf(connection).pending = false;
    writeEventOf(connection).pending = false;
    bench.complete(connection, completion::timeout());
    EXPECT_EQ(net::PENDING_WORKING, connection.state().pending);
    EXPECT_FALSE(connection.state().isSettled());
}

TEST(Connection, WorkReadyCompletionStartsWorkAndCallsOnWorkStartedWithStreamHandle)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenWork());
    bench.opened(connection);
    net::IoAction started = bench.complete(connection, completion::workReady());
    EXPECT_EQ(net::IoActionKind::NONE, started.kind);
    ASSERT_EQ(1u, bench.recorder.count(Call::ON_WORK_STARTED));
    const kiotty_test::Record& work = bench.recorder.last(Call::ON_WORK_STARTED);
    EXPECT_EQ(&bench.recorder, work.connection_context);
    EXPECT_EQ(&bench.owner, work.stream.owner);
    EXPECT_EQ(connection.state().id.value(), work.stream.id);
    EXPECT_TRUE(isWorking(connection));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.verify(connection.state().id.generation));
}

TEST(Connection, ApplyCloseAbandonsRequestedWork)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenHold(0));
    bench.recorder.then(thenWork());
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "n"));
    net::IoAction push = bench.pick(connection);
    writeEventOf(connection).pending = true;
    bench.complete(connection, completion::timeout());
    ASSERT_TRUE(isWorking(connection));
    ASSERT_FALSE(connection.flows().work.isReady());
    writeEventOf(connection).pending = false;
    net::IoAction closed = bench.complete(connection, completion::sent(push, 0, net::SocketCode::RESET));
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_FALSE(isWorking(connection));
    EXPECT_FALSE(connection.flows().work.isReady());
    EXPECT_EQ(0u, bench.recorder.count(Call::ON_WORK_STARTED));
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_DISCONNECTED));
}

TEST(Connection, ApplyCloseDuringWorkingKeepsWorkUntilCloseStream)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    net::IoAction closed = connection.close(KIOTTY_NET_CODE_CLOSED, bench.handler);
    EXPECT_EQ(net::IoActionKind::CLOSE, closed.kind);
    EXPECT_EQ(net::Phase::CLOSING, connection.state().phase);
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_DISCONNECTED));
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, bench.recorder.last(Call::ON_DISCONNECTED).code);
    EXPECT_TRUE(isWorking(connection));
    EXPECT_EQ(net::IoActionKind::NONE, connection.close(KIOTTY_NET_CODE_CLOSED, bench.handler).kind);
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_DISCONNECTED));
}

TEST(Connection, StreamReturnsConnectionStreamOnlyWhileWorking)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenWork());
    bench.opened(connection);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, connection.flows().work.verify(connection.state().id.generation));
    bench.complete(connection, completion::workReady());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.verify(connection.state().id.generation));
    net::ConnectionStream stream = connection.flows().work.stream();
    (void)stream;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenHold(0)));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, connection.flows().work.verify(connection.state().id.generation));
}

TEST(Connection, CloseStreamVerifyIsBusyWhenNotWorking)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenWork());
    bench.opened(connection);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, connection.flows().work.closeStream(connection.state().id.generation, thenRead(4, 0)));
    bench.complete(connection, completion::workReady());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenRead(4, 0)));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, connection.flows().work.closeStream(connection.state().id.generation, thenClose(KIOTTY_NET_CODE_SUCCESS)));
    net::IoAction outcome = connection.flows().work.takeOutcome();
    EXPECT_EQ(net::IoActionKind::READ, outcome.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, connection.flows().work.closeStream(connection.state().id.generation, thenClose(KIOTTY_NET_CODE_SUCCESS)));
}

TEST(Connection, CloseStreamVerifyIsPeerClosedForAnOldGeneration)
{
    Bench bench(smallLimits());
    net::Connection& first = bench.open();
    startWork(bench, first);
    int32_t old_generation = first.state().id.generation;
    bench.host.release(first);
    net::Connection& second = bench.open();
    startWork(bench, second);
    ASSERT_NE(old_generation, second.state().id.generation);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, second.flows().work.closeStream(old_generation, thenWriteSpan(spanOf("stale"), 0)));
    EXPECT_FALSE(second.flows().work.isFinished());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, second.flows().work.closeStream(second.state().id.generation, thenHold(0)));
    bench.complete(second, completion::workDone(second.flows().work.takeOutcome()));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(second).kind);
}

TEST(Connection, CloseStreamVerifyIsSuccessAndGoesDone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    int32_t generation = connection.state().id.generation;
    KiottyNetCode closed = KIOTTY_NET_CODE_BUSY;
    std::thread worker([&connection, generation, &closed]() { closed = connection.flows().work.closeStream(generation, thenRead(4, 0)); });
    worker.join();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, closed);
    EXPECT_TRUE(connection.flows().work.isFinished());
}

TEST(Connection, WorkDoneDrivesTheOutcomeThatCloseStreamStored)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenRead(4, 700)));
    net::IoAction outcome = connection.flows().work.takeOutcome();
    EXPECT_FALSE(isWorking(connection));
    EXPECT_FALSE(connection.flows().work.isFinished());
    uint64_t before = kiotty::net::implementation::monotonicMilliseconds();
    net::IoAction read = bench.complete(connection, completion::workDone(outcome));
    uint64_t after = kiotty::net::implementation::monotonicMilliseconds();
    EXPECT_EQ(net::IoActionKind::READ, read.kind);
    EXPECT_EQ(4u, read.as.read.exact);
    expectDeadlineAround(connection, before, after, 700);
}

TEST(Connection, CloseStreamWithThenWriteStagesOnTheWorkerAndWorkDoneReturnsHold)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    int32_t generation = connection.state().id.generation;
    std::string response = "resp";
    KiottyNetCode closed = KIOTTY_NET_CODE_BUSY;
    std::thread worker([&]() { closed = connection.flows().work.closeStream(generation, thenWriteSpan(spanOf(response), 100)); });
    worker.join();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, closed);
    net::IoAction outcome = connection.flows().work.takeOutcome();
    EXPECT_EQ(net::IoActionKind::HOLD, outcome.kind);
    EXPECT_EQ(100u, outcome.timeout_milliseconds);
    net::IoAction held = bench.complete(connection, completion::workDone(outcome));
    EXPECT_EQ(net::IoActionKind::HOLD, held.kind);
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::USER, write.purpose);
    EXPECT_EQ("resp", payloadOf(connection, write));
}

TEST(Connection, CloseStreamWithThenReadAlreadySatisfiedCallsOnReadAtWorkDone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.then(thenRead(2, 0));
    bench.recorder.then(thenWork());
    net::IoAction read = bench.opened(connection);
    net::IoAction requested = bench.deliver(connection, read, "abcdef");
    ASSERT_EQ(net::IoActionKind::START_WORK, requested.kind);
    bench.complete(connection, completion::workReady());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenRead(4, 0)));
    bench.complete(connection, completion::workDone(connection.flows().work.takeOutcome()));
    ASSERT_EQ(2u, bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("cdef", bench.recorder.last(Call::ON_READ).data);
}

TEST(Connection, CloseStreamWithThenWorkReturnsInvalidArgumentAndStreamStaysValid)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, connection.flows().work.closeStream(connection.state().id.generation, thenWork()));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.verify(connection.state().id.generation));
    EXPECT_FALSE(connection.flows().work.isFinished());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenClose(KIOTTY_NET_CODE_SUCCESS)));
}

TEST(Connection, CloseStreamWithWriteLargerThanSendCapacityReturnsBufferFullAndStreamStaysValid)
{
    Bench bench(limitsOf(64, 8, 64));
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    std::string large(9, 'w');
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, connection.flows().work.closeStream(connection.state().id.generation, thenWriteSpan(spanOf(large), 0)));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.verify(connection.state().id.generation));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenHold(0)));
    bench.complete(connection, completion::workDone(connection.flows().work.takeOutcome()));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
}

TEST(Connection, CloseStreamDuringClosingReturnsClosedButGoesDone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    connection.close(KIOTTY_NET_CODE_CLOSED, bench.handler);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, connection.flows().work.closeStream(connection.state().id.generation, thenHold(0)));
    EXPECT_TRUE(connection.flows().work.isFinished());
    net::IoAction outcome = connection.flows().work.takeOutcome();
    EXPECT_EQ(net::IoActionKind::NONE, bench.complete(connection, completion::workDone(outcome)).kind);
    EXPECT_FALSE(isWorking(connection));
    EXPECT_EQ(1u, bench.recorder.count(Call::ON_DISCONNECTED));
}

TEST(Connection, NotifyDuringWorkIsStagedAndPickedOnlyAfterWorkDone)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    startWork(bench, connection);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "n"));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.closeStream(connection.state().id.generation, thenHold(0)));
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(connection).kind);
    bench.complete(connection, completion::workDone(connection.flows().work.takeOutcome()));
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::NOTIFY, write.purpose);
    EXPECT_EQ("n", payloadOf(connection, write));
}

TEST(Connection, NotifyStagesPlaintextIntoTheNotifyStage)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "hello"));
    net::IoAction write = bench.pick(connection);
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(5u, write.as.write.length);
    EXPECT_EQ("hello", payloadOf(connection, write));
}

TEST(Connection, NotifyOnIdleOrClosingConnectionReturnsPeerClosed)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.opened(connection);
    kiotty::net::implementation::PoolHandle old_id = connection.state().id;
    bench.host.release(connection);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.desk.notify(old_id, spanOf("x")));
    net::Connection& reopened = bench.open();
    bench.opened(reopened);
    reopened.close(KIOTTY_NET_CODE_CLOSED, bench.handler);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, bench.notify(reopened, "x"));
}

TEST(Connection, NotifyBeyondPushCapacityReturnsBufferFullAndWritesNothing)
{
    Bench bench(limitsOf(64, 64, 8));
    net::Connection& connection = bench.open();
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "12345678"));
    EXPECT_EQ(KIOTTY_NET_CODE_BUFFER_FULL, bench.notify(connection, "9"));
    net::IoAction write = bench.pick(connection);
    EXPECT_EQ(8u, write.as.write.length);
    EXPECT_EQ("12345678", payloadOf(connection, write));
}

TEST(Connection, NotifyFromAnotherThreadAThousandTimesWhilePickAndReleaseRunKeepsEveryByteInOrder)
{
    Bench bench(limitsOf(64, 64, 64));
    net::Connection& connection = bench.open();
    bench.opened(connection);
    const int messages = 1000;
    std::string expected;
    for (int i = 0; i < messages; ++i)
    {
        expected += std::to_string(10000 + i);
    }
    std::thread pusher([&]() {
        for (int i = 0; i < messages; ++i)
        {
            std::string message = std::to_string(10000 + i);
            while (bench.notify(connection, message) == KIOTTY_NET_CODE_BUFFER_FULL)
            {
                std::this_thread::yield();
            }
        }
    });
    std::string received;
    while (received.size() < expected.size())
    {
        net::IoAction write = bench.pick(connection);
        if (write.kind != net::IoActionKind::WRITE)
        {
            std::this_thread::yield();
            continue;
        }
        received += payloadOf(connection, write);
        bench.complete(connection, completion::sent(write, write.as.write.length));
    }
    pusher.join();
    EXPECT_EQ(expected, received);
}

TEST(Connection, ReleaseReturnsToIdleClearsStagesAndResetsWork)
{
    Bench bench(smallLimits());
    net::Connection& connection = bench.open();
    bench.recorder.thenWrite("user", 100);
    bench.opened(connection);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, bench.notify(connection, "push"));
    bench.recorder.then(thenWork());
    bench.complete(connection, completion::sent(net::IoAction::write(net::IoPurpose::USER, 0, 4), 4));
    ASSERT_TRUE(isWorking(connection));
    int32_t before = connection.state().id.generation;
    bench.host.release(connection);
    EXPECT_EQ(net::Phase::IDLE, connection.state().phase);
    EXPECT_FALSE(connection.state().is_alive);
    EXPECT_FALSE(isWorking(connection));
    EXPECT_EQ(0u, connection.state().deadline);
    net::Connection& reopened = bench.open();
    EXPECT_NE(before, reopened.state().id.generation);
    bench.opened(reopened);
    EXPECT_EQ(net::IoActionKind::NONE, bench.pick(reopened).kind);
    EXPECT_FALSE(isWorking(reopened));
}

TEST(Connection, MachineStateStartsFromTheHandlerAndTheCallerCarriesItBetweenEvents)
{
    Bench bench(smallLimits());
    StateProbe probe(bench.owner, 7);
    net::Connection& connection = bench.open();
    EXPECT_EQ(net::IoActionKind::HOLD, connection.onOpened(probe).kind);
    connection.close(KIOTTY_NET_CODE_SUCCESS, probe);
    EXPECT_EQ((std::vector<int32_t>{ 7, 8 }), probe.states);
    EXPECT_EQ((std::vector<KiottyNetConnectionEventType>{ KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED, KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED }), probe.types);
    bench.host.release(connection);
    StateProbe second(bench.owner, 3);
    net::Connection& reopened = bench.open();
    reopened.onOpened(second);
    EXPECT_EQ((std::vector<int32_t>{ 3 }), second.states);
}

TEST(Connection, WorkStartedReturningReadIsAppliedToTheStreamWithoutAWorker)
{
    Bench bench(smallLimits());
    StateProbe probe(bench.owner, 0);
    probe.on_connected = thenWork();
    probe.on_work_started = thenRead(4, 0);
    net::Connection& connection = bench.open();
    EXPECT_EQ(net::IoActionKind::START_WORK, connection.onOpened(probe).kind);
    EXPECT_EQ(net::IoActionKind::NONE, connection.dispatch(completion::workReady(), probe, bench.host.notificationMutex()).kind);
    ASSERT_TRUE(connection.flows().work.isFinished());
    net::IoAction outcome = connection.flows().work.takeOutcome();
    EXPECT_EQ(net::IoActionKind::READ, outcome.kind);
    EXPECT_TRUE(probe.reported.empty());
}

TEST(Connection, WorkStartedReturningStartWorkLeavesTheStreamToTheWorker)
{
    Bench bench(smallLimits());
    StateProbe probe(bench.owner, 0);
    probe.on_connected = thenWork();
    net::Connection& connection = bench.open();
    connection.onOpened(probe);
    connection.dispatch(completion::workReady(), probe, bench.host.notificationMutex());
    EXPECT_FALSE(connection.flows().work.isFinished());
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, connection.flows().work.verify(connection.state().id.generation));
}

TEST(Connection, WorkStartedReturningAnInvalidActionIsReportedAndTheStreamStaysOpen)
{
    Bench bench(smallLimits());
    StateProbe probe(bench.owner, 0);
    probe.on_connected = thenWork();
    probe.on_work_started = thenRead(0, 0);
    net::Connection& connection = bench.open();
    connection.onOpened(probe);
    connection.dispatch(completion::workReady(), probe, bench.host.notificationMutex());
    EXPECT_FALSE(connection.flows().work.isFinished());
    EXPECT_EQ((std::vector<KiottyNetCode>{ KIOTTY_NET_CODE_INVALID_ARGUMENT }), probe.reported);
}
