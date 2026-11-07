#include "data/repository/connection/kiotty_work_context.h"
#include "data/repository/connection/kiotty_work_reporter.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

using kiotty::net::implementation::IoAction;
using kiotty::net::implementation::IoActionKind;
using kiotty::net::implementation::IoPurpose;
using kiotty::net::implementation::WorkContext;
using kiotty::net::implementation::WorkReporter;

namespace
{
    using State = WorkContext::State;
}

TEST(WorkContext, RequestStartFinishTakeOutcomeCyclesIdleRequestedWorkingDoneIdle)
{
    WorkContext work;
    WorkReporter reporter(work);
    EXPECT_EQ(State::IDLE, work.state());
    work.request();
    EXPECT_EQ(State::REQUESTED, work.state());
    work.start(7);
    EXPECT_EQ(State::WORKING, work.state());
    reporter.finish(IoAction::read(IoPurpose::USER, 250, 4));
    EXPECT_EQ(State::DONE, work.state());
    IoAction outcome = work.takeOutcome();
    EXPECT_EQ(IoActionKind::READ, outcome.kind);
    EXPECT_EQ(4u, outcome.as.read.exact);
    EXPECT_EQ(250u, outcome.timeout_milliseconds);
    EXPECT_EQ(State::IDLE, work.state());
}

TEST(WorkContext, AbandonTurnsRequestedBackToIdle)
{
    WorkContext work;
    work.request();
    work.abandon();
    EXPECT_EQ(State::IDLE, work.state());
}

TEST(WorkContext, AbandonInWorkingOrDoneDoesNothing)
{
    WorkContext work;
    WorkReporter reporter(work);
    work.abandon();
    EXPECT_EQ(State::IDLE, work.state());
    work.request();
    work.start(1);
    work.abandon();
    EXPECT_EQ(State::WORKING, work.state());
    reporter.finish(IoAction::hold(0));
    work.abandon();
    EXPECT_EQ(State::DONE, work.state());
}

TEST(WorkContext, TakeOutcomeBeforeDoneIsNoneAndKeepsTheState)
{
    WorkContext work;
    work.request();
    work.start(2);
    EXPECT_EQ(IoActionKind::NONE, work.takeOutcome().kind);
    EXPECT_EQ(State::WORKING, work.state());
}

TEST(WorkReporter, VerifyIsSuccessOnlyWhenWorkingWithTheSameGeneration)
{
    WorkContext work;
    WorkReporter reporter(work);
    work.request();
    work.start(3);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, reporter.verify(3));
}

TEST(WorkReporter, VerifyInIdleRequestedOrDoneIsBusy)
{
    WorkContext work;
    WorkReporter reporter(work);
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, reporter.verify(0));
    work.request();
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, reporter.verify(0));
    work.start(5);
    reporter.finish(IoAction::hold(0));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, reporter.verify(5));
}

TEST(WorkReporter, VerifyWithDifferentGenerationIsPeerClosed)
{
    WorkContext work;
    WorkReporter reporter(work);
    work.request();
    work.start(5);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, reporter.verify(4));
}

TEST(WorkReporter, FinishFromAnotherThreadIsSeenAsDoneWithItsOutcome)
{
    WorkContext work;
    for (int i = 1; i <= 1000; ++i)
    {
        work.request();
        work.start(i);
        size_t exact = static_cast<size_t>(i);
        std::thread worker([&work, i, exact]() {
            WorkReporter reporter(work);
            if (reporter.verify(i) == KIOTTY_NET_CODE_SUCCESS)
            {
                reporter.finish(IoAction::read(IoPurpose::USER, static_cast<uint32_t>(i), exact));
            }
        });
        while (work.state() != State::DONE)
        {
            std::this_thread::yield();
        }
        IoAction outcome = work.takeOutcome();
        worker.join();
        ASSERT_EQ(IoActionKind::READ, outcome.kind);
        ASSERT_EQ(exact, outcome.as.read.exact);
        ASSERT_EQ(static_cast<uint32_t>(i), outcome.timeout_milliseconds);
    }
}

TEST(WorkContext, ResetReturnsToIdleFromEveryState)
{
    WorkContext work;
    WorkReporter reporter(work);
    work.request();
    work.reset();
    EXPECT_EQ(State::IDLE, work.state());
    work.request();
    work.start(1);
    work.reset();
    EXPECT_EQ(State::IDLE, work.state());
    work.request();
    work.start(2);
    reporter.finish(IoAction::hold(0));
    work.reset();
    EXPECT_EQ(State::IDLE, work.state());
    EXPECT_EQ(IoActionKind::NONE, work.takeOutcome().kind);
}
