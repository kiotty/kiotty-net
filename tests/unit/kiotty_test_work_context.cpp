#include "data/repository/connection/kiotty_work_context.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

using kiotty::net::implementation::IoAction;
using kiotty::net::implementation::IoActionKind;
using kiotty::net::implementation::IoPurpose;
using kiotty::net::implementation::WorkContext;

namespace
{
    void expectFlags(const WorkContext& work, bool requested, bool working, bool finished)
    {
        EXPECT_EQ(requested, work.requested());
        EXPECT_EQ(working, work.working());
        EXPECT_EQ(finished, work.finished());
    }
}

TEST(WorkContext, RequestStartFinishOnFinishedCyclesIdleRequestedWorkingDoneIdle)
{
    WorkContext work;
    expectFlags(work, false, false, false);
    work.request();
    expectFlags(work, true, true, false);
    work.start(7);
    expectFlags(work, false, true, false);
    work.finish(IoAction::read(IoPurpose::USER, 250, 4));
    expectFlags(work, false, true, true);
    IoAction outcome = work.onFinished();
    EXPECT_EQ(IoActionKind::READ, outcome.kind);
    EXPECT_EQ(4u, outcome.as.read.exact);
    EXPECT_EQ(250u, outcome.timeout_milliseconds);
    expectFlags(work, false, false, false);
}

TEST(WorkContext, AbandonTurnsRequestedBackToIdle)
{
    WorkContext work;
    work.request();
    work.abandon();
    expectFlags(work, false, false, false);
}

TEST(WorkContext, AbandonInWorkingOrDoneDoesNothing)
{
    WorkContext work;
    work.abandon();
    expectFlags(work, false, false, false);
    work.request();
    work.start(1);
    work.abandon();
    expectFlags(work, false, true, false);
    work.finish(IoAction::hold(0));
    work.abandon();
    expectFlags(work, false, true, true);
}

TEST(WorkContext, VerifyIsSuccessOnlyWhenWorkingWithTheSameGeneration)
{
    WorkContext work;
    work.request();
    work.start(3);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, work.verify(3));
}

TEST(WorkContext, VerifyInIdleRequestedOrDoneIsBusy)
{
    WorkContext work;
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, work.verify(0));
    work.request();
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, work.verify(0));
    work.start(5);
    work.finish(IoAction::hold(0));
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, work.verify(5));
}

TEST(WorkContext, VerifyWithDifferentGenerationIsPeerClosed)
{
    WorkContext work;
    work.request();
    work.start(5);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, work.verify(4));
}

TEST(WorkContext, FinishFromAnotherThreadIsSeenAsFinishedWithItsOutcome)
{
    WorkContext work;
    for (int i = 1; i <= 1000; ++i)
    {
        work.request();
        work.start(i);
        size_t exact = static_cast<size_t>(i);
        std::thread worker([&work, i, exact]() {
            if (work.verify(i) == KIOTTY_NET_CODE_SUCCESS)
            {
                work.finish(IoAction::read(IoPurpose::USER, static_cast<uint32_t>(i), exact));
            }
        });
        while (!work.finished())
        {
            std::this_thread::yield();
        }
        IoAction outcome = work.onFinished();
        worker.join();
        ASSERT_EQ(IoActionKind::READ, outcome.kind);
        ASSERT_EQ(exact, outcome.as.read.exact);
        ASSERT_EQ(static_cast<uint32_t>(i), outcome.timeout_milliseconds);
    }
}

TEST(WorkContext, ResetReturnsToIdleFromEveryState)
{
    WorkContext work;
    work.request();
    work.reset();
    expectFlags(work, false, false, false);
    work.request();
    work.start(1);
    work.reset();
    expectFlags(work, false, false, false);
    work.request();
    work.start(2);
    work.finish(IoAction::hold(0));
    work.reset();
    expectFlags(work, false, false, false);
    EXPECT_EQ(IoActionKind::NONE, work.onFinished().kind);
}
