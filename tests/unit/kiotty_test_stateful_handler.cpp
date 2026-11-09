#include "domain/usecases/statemachine/kiotty_stateful_handler.h"

#include "data/repository/connection/kiotty_connection.h"
#include "support/kiotty_test_completion.h"
#include "support/kiotty_test_handler.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace net = kiotty::net::implementation;
namespace completion = kiotty_test::completion;

namespace
{
    typedef kiotty_test::ConnectionBench<1> Bench;

    enum class State : int32_t
    {
        IDLE,
        READING,
        WRITING,
        WORKING
    };

    struct App
    {
        int                        created = 0;
        int                        released = 0;
        int                        closed = 0;
        bool                       has_session = true;
        State                      connected_next = State::READING;
        KiottyNetConnectionAction  connected_action = KiottyNet_thenRead(4, 0);
        KiottyNetConnectionAction  work_action = KiottyNet_thenRead(4, 0);
        std::string                read;
        std::vector<KiottyNetCode> errors;
        int                        session = 0;
    };

    App* current = nullptr;

    typedef KiottyNet_Transition<State> Next;

    KiottyNetTransition onConnected(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return Next::moveToNextState(current->connected_next, current->connected_action);
    }

    KiottyNetTransition onRead(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent event)
    {
        current->read.assign(event.as.read.data.data, event.as.read.data.length);
        return Next::moveToNextState(State::WRITING, KiottyNet_thenHold(0));
    }

    KiottyNetTransition onWorkStarted(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return Next::moveToNextState(State::READING, current->work_action);
    }

    KiottyNetTransition onClosed(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        ++current->closed;
        return Next::moveToNextState(State::WORKING, KiottyNet_thenRead(1, 0));
    }

    KiottyNetConnectionContext create(KiottyNetAppContext app_context, KiottyNetConnectionInfo)
    {
        App& app = *static_cast<App*>(app_context);
        ++app.created;
        return app.has_session ? &app.session : nullptr;
    }

    void release(KiottyNetAppContext app_context, KiottyNetConnectionContext, KiottyNetConnectionHandle)
    {
        ++static_cast<App*>(app_context)->released;
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo, KiottyNetCode code, const char*)
    {
        static_cast<App*>(app_context)->errors.push_back(code);
    }

    constexpr auto RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(onConnected).to(State::READING, State::WORKING),
        KiottyNet_from(State::READING).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(onRead).to(State::WRITING),
        KiottyNet_from(State::WORKING).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WORK_STARTED).then(onWorkStarted).to(State::READING),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(onClosed));
    constexpr KiottyNetStateMachine MACHINE = KiottyNet_makeStateMachine(RULES, State::IDLE);

    constexpr auto RULES_WITHOUT_CLOSED = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(onConnected).to(State::READING));
    constexpr KiottyNetStateMachine MACHINE_WITHOUT_CLOSED = KiottyNet_makeStateMachine(RULES_WITHOUT_CLOSED, State::IDLE);

    class StatefulHandlerTest : public ::testing::Test
    {
    protected:
        StatefulHandlerTest()
            : bench(kiotty_test::limitsOf(64, 64, 64))
            , app()
            , stateful{ &app, &MACHINE, create, release, onError }
            , handler(stateful, bench.owner)
        {
            current = &app;
        }

        ~StatefulHandlerTest() override
        {
            current = nullptr;
        }

        net::IoAction deliver(net::Connection& connection, const net::IoAction& read, const std::string& bytes)
        {
            const size_t length = bench.place(connection, read, bytes);
            return connection.dispatch(completion::receive(read, length), handler, bench.host.notificationMutex());
        }

        net::IoAction step(net::Connection& connection, const net::IoCompletion& done)
        {
            return connection.dispatch(done, handler, bench.host.notificationMutex());
        }

        Bench                       bench;
        App                         app;
        KiottyNetStatefulAppContext stateful;
        net::StatefulHandler        handler;
    };
}

TEST_F(StatefulHandlerTest, ValidateChecksTheTableAndInitialStateIsTheMachineInitial)
{
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, handler.validate());
    EXPECT_EQ(static_cast<int32_t>(State::IDLE), handler.initialState());
    KiottyNetStatefulAppContext empty{ &app, nullptr, nullptr, nullptr, nullptr };
    const net::StatefulHandler without_machine(empty, bench.owner);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, without_machine.validate());
}

TEST_F(StatefulHandlerTest, DispatchMovesStateAndReturnsEntryAction)
{
    net::Connection& connection = bench.open();
    const net::IoAction read = connection.onOpened(handler);
    ASSERT_EQ(net::IoActionKind::READ, read.kind);
    EXPECT_EQ(net::IoActionKind::HOLD, deliver(connection, read, "abcd").kind);
    EXPECT_EQ("abcd", app.read);
    EXPECT_TRUE(app.errors.empty());
}

TEST_F(StatefulHandlerTest, DispatchClosedIgnoresTransitionAndReleases)
{
    net::Connection& connection = bench.open();
    connection.onOpened(handler);
    connection.close(KIOTTY_NET_CODE_SUCCESS, handler);
    EXPECT_EQ(1, app.closed);
    EXPECT_EQ(1, app.released);
    EXPECT_TRUE(app.errors.empty());
}

TEST_F(StatefulHandlerTest, DispatchClosedWithoutARowStillReleases)
{
    stateful.machine = &MACHINE_WITHOUT_CLOSED;
    net::Connection& connection = bench.open();
    connection.onOpened(handler);
    connection.close(KIOTTY_NET_CODE_SUCCESS, handler);
    EXPECT_EQ(0, app.closed);
    EXPECT_EQ(1, app.released);
    EXPECT_TRUE(app.errors.empty());
}

TEST_F(StatefulHandlerTest, IllegalTransitionReportsAndCloses)
{
    app.connected_next = State::WRITING;
    net::Connection& connection = bench.open();
    const net::IoAction closing = connection.onOpened(handler);
    ASSERT_EQ(net::IoActionKind::CLOSE, closing.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_ILLEGAL_TRANSITION, closing.as.close.reason);
    EXPECT_EQ((std::vector<KiottyNetCode>{ KIOTTY_NET_CODE_ILLEGAL_TRANSITION }), app.errors);
    EXPECT_EQ(1, app.released);
}

TEST_F(StatefulHandlerTest, UnhandledEventReportsAndCloses)
{
    net::Connection& connection = bench.open();
    connection.onOpened(handler);
    const net::IoAction closing = step(connection, completion::timeout());
    ASSERT_EQ(net::IoActionKind::CLOSE, closing.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_UNHANDLED_EVENT, closing.as.close.reason);
    EXPECT_EQ((std::vector<KiottyNetCode>{ KIOTTY_NET_CODE_UNHANDLED_EVENT }), app.errors);
    EXPECT_EQ(1, app.closed);
}

TEST_F(StatefulHandlerTest, AdmitWithoutCreateAcceptsNullSession)
{
    stateful.createConnectionContext = nullptr;
    const KiottyNetConnectionDecision decision = handler.admit(KiottyNetConnectionInfo{});
    EXPECT_TRUE(decision.accepted);
    EXPECT_EQ(nullptr, decision.connection_context);
}

TEST_F(StatefulHandlerTest, AdmitWithNullSessionReportsSessionExhaustedAndIgnores)
{
    app.has_session = false;
    const KiottyNetConnectionDecision decision = handler.admit(KiottyNetConnectionInfo{});
    EXPECT_FALSE(decision.accepted);
    EXPECT_EQ(1, app.created);
    EXPECT_EQ((std::vector<KiottyNetCode>{ KIOTTY_NET_CODE_SESSION_EXHAUSTED }), app.errors);
    app.has_session = true;
    const KiottyNetConnectionDecision accepted = handler.admit(KiottyNetConnectionInfo{});
    EXPECT_TRUE(accepted.accepted);
    EXPECT_EQ(&app.session, accepted.connection_context);
}

TEST_F(StatefulHandlerTest, OnErrorNullIsAllowed)
{
    stateful.onError = nullptr;
    app.has_session = false;
    EXPECT_FALSE(handler.admit(KiottyNetConnectionInfo{}).accepted);
    EXPECT_TRUE(app.errors.empty());
}

TEST_F(StatefulHandlerTest, WorkStartedReturningReadIsAppliedToTheStream)
{
    app.connected_next = State::WORKING;
    app.connected_action = KiottyNet_thenWork();
    net::Connection& connection = bench.open();
    ASSERT_EQ(net::IoActionKind::START_WORK, connection.onOpened(handler).kind);
    EXPECT_EQ(net::IoActionKind::NONE, step(connection, completion::workReady()).kind);
    ASSERT_TRUE(connection.flows().work.isFinished());
    const net::IoAction outcome = connection.flows().work.takeOutcome();
    ASSERT_EQ(net::IoActionKind::READ, outcome.kind);
    EXPECT_EQ(net::IoActionKind::READ, step(connection, completion::workDone(outcome)).kind);
    EXPECT_TRUE(app.errors.empty());
}

TEST_F(StatefulHandlerTest, WorkStartedWithoutARowClosesTheStreamWithUnhandledEvent)
{
    stateful.machine = &MACHINE_WITHOUT_CLOSED;
    app.connected_action = KiottyNet_thenWork();
    net::Connection& connection = bench.open();
    ASSERT_EQ(net::IoActionKind::START_WORK, connection.onOpened(handler).kind);
    step(connection, completion::workReady());
    ASSERT_TRUE(connection.flows().work.isFinished());
    const net::IoAction outcome = connection.flows().work.takeOutcome();
    ASSERT_EQ(net::IoActionKind::CLOSE, outcome.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_UNHANDLED_EVENT, outcome.as.close.reason);
    EXPECT_EQ((std::vector<KiottyNetCode>{ KIOTTY_NET_CODE_UNHANDLED_EVENT }), app.errors);
}
