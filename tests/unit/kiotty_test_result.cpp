#include "core/kiotty_result.h"

#include "support/kiotty_test_tracked.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>

using kiotty::Result;
using kiotty_test::Ledger;
using kiotty_test::Tracked;

namespace
{
    enum class SendCode
    {
        Ok        = 0,
        Refused   = 1,
        Exhausted = 2,
    };

    typedef Result<SendCode, Tracked> TrackedResult;

    TrackedResult okWith(Ledger& ledger, int id)
    {
        return kiotty::ok(Tracked(ledger, id));
    }

    TrackedResult failWith(SendCode code)
    {
        return kiotty::error(code);
    }

    struct AssignCase
    {
        bool        target_ok;
        bool        source_ok;
        bool        by_move;
        const char* name;
        const char* label;
    };

    const AssignCase kAssignCases[] =
    {
        { true,  true,  false, "CopyOkFromOk",     "copy: ok <- ok" },
        { true,  false, false, "CopyOkFromFail",   "copy: ok <- fail" },
        { false, true,  false, "CopyFailFromOk",   "copy: fail <- ok" },
        { false, false, false, "CopyFailFromFail", "copy: fail <- fail" },
        { true,  true,  true,  "MoveOkFromOk",     "move: ok <- ok" },
        { true,  false, true,  "MoveOkFromFail",   "move: ok <- fail" },
        { false, true,  true,  "MoveFailFromOk",   "move: fail <- ok" },
        { false, false, true,  "MoveFailFromFail", "move: fail <- fail" },
    };

    class Assign : public ::testing::TestWithParam<AssignCase>
    {
    };

    std::string nameOf(const ::testing::TestParamInfo<AssignCase>& info)
    {
        return info.param.name;
    }
}

TEST(Result, OkResultReportsSuccessAndCarriesTheValue)
{
    Ledger ledger;

    {
        const TrackedResult result = okWith(ledger, 5);

        EXPECT_TRUE(result.isOk());
        EXPECT_EQ(SendCode::Ok, result.code());
        EXPECT_EQ(5, result.value().id());
        EXPECT_EQ(1, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Result, FailedResultCarriesTheCodeAndConstructsNoValue)
{
    Ledger ledger;

    {
        const TrackedResult result = failWith(SendCode::Refused);

        EXPECT_FALSE(result.isOk());
        EXPECT_EQ(SendCode::Refused, result.code());

        EXPECT_EQ(0, ledger.acquired);
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Result, OperatorBoolReportsFailureRatherThanSuccess)
{
    Ledger ledger;

    const TrackedResult good = okWith(ledger, 6);
    const TrackedResult bad  = failWith(SendCode::Exhausted);

    EXPECT_FALSE(static_cast<bool>(good));
    EXPECT_TRUE(static_cast<bool>(bad));

    EXPECT_TRUE(good.isOk());
    EXPECT_FALSE(bad.isOk());
}

TEST(Result, CopyingAnOkResultProducesAnIndependentValue)
{
    Ledger ledger;

    {
        const TrackedResult source = okWith(ledger, 7);
        const int           before = ledger.copies;

        const TrackedResult copy(source);

        EXPECT_TRUE(copy.isOk());
        EXPECT_EQ(7, copy.value().id());
        EXPECT_EQ(before + 1, ledger.copies);

        EXPECT_EQ(2, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Result, CopyingAFailedResultStartsNoValue)
{
    Ledger ledger;

    {
        const TrackedResult source = failWith(SendCode::Refused);
        const TrackedResult copy(source);

        EXPECT_FALSE(copy.isOk());
        EXPECT_EQ(SendCode::Refused, copy.code());
        EXPECT_EQ(0, ledger.acquired);
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Result, MovingAnOkResultMovesTheValueRatherThanCopyingIt)
{
    Ledger ledger;

    {
        TrackedResult source = okWith(ledger, 8);

        const int copies_before = ledger.copies;

        const TrackedResult moved(std::move(source));

        EXPECT_TRUE(moved.isOk());
        EXPECT_EQ(8, moved.value().id());
        EXPECT_EQ(copies_before, ledger.copies) << "the move copied instead";

        EXPECT_TRUE(source.isOk());
        EXPECT_EQ(1, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Result, MovingAFailedResultStartsNoValue)
{
    Ledger ledger;

    {
        TrackedResult       source = failWith(SendCode::Exhausted);
        const TrackedResult moved(std::move(source));

        EXPECT_FALSE(moved.isOk());
        EXPECT_EQ(SendCode::Exhausted, moved.code());
        EXPECT_EQ(0, ledger.acquired);
    }

    EXPECT_EQ(0, ledger.live());
}

TEST_P(Assign, LeavesNoValueBehindWhicheverWayItGoes)
{
    const AssignCase& sample = GetParam();

    SCOPED_TRACE(sample.label);

    Ledger ledger;

    {
        TrackedResult target = sample.target_ok ? okWith(ledger, 1)
                                                : failWith(SendCode::Refused);
        TrackedResult source = sample.source_ok ? okWith(ledger, 2)
                                                : failWith(SendCode::Exhausted);

        if (sample.by_move)
        {
            target = std::move(source);
        }
        else
        {
            target = source;
        }

        EXPECT_EQ(sample.source_ok, target.isOk());

        if (sample.source_ok)
        {
            EXPECT_EQ(2, target.value().id());
        }
        else
        {
            EXPECT_EQ(SendCode::Exhausted, target.code());
        }
    }

    EXPECT_EQ(0, ledger.live());
}

INSTANTIATE_TEST_SUITE_P(EveryAssignmentCombination, Assign,
                         ::testing::ValuesIn(kAssignCases), nameOf);

TEST(Result, SelfAssignmentKeepsTheValue)
{
    Ledger ledger;

    {
        TrackedResult        result = okWith(ledger, 9);
        const TrackedResult& alias  = result;

        result = alias;

        EXPECT_TRUE(result.isOk());
        EXPECT_EQ(9, result.value().id());
        EXPECT_EQ(1, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Result, WorksWithAPlainIntegralErrorCode)
{
    const Result<int, int> good = kiotty::ok(42);
    const Result<int, int> bad  = kiotty::error(7);

    EXPECT_TRUE(good.isOk());
    EXPECT_EQ(42, good.value());
    EXPECT_FALSE(bad.isOk());
    EXPECT_EQ(7, bad.code());
}
