#include "core/kiotty_holder.h"

#include "support/kiotty_test_tracked.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>

using kiotty::Holder;
using kiotty_test::Ledger;
using kiotty_test::Tracked;

namespace
{
    class Shape
    {
    public:
        static const size_t HOLDER_SIZE  = 64;
        static const size_t HOLDER_ALIGN = 8;

        virtual ~Shape() {}

        virtual int sides() const = 0;
    };

    class Triangle : public Shape
    {
    public:
        Triangle(Ledger& ledger, int id) :
            _resource(ledger, id)
        {
        }

        Triangle(Triangle&& other) noexcept :
            Shape(other),
            _resource(std::move(other._resource))
        {
        }

        int sides() const override { return 3; }

        int id() const { return _resource.id(); }

    private:
        Tracked _resource;
    };

    class Square : public Shape
    {
    public:
        Square(Ledger& ledger, int id) :
            _resource(ledger, id)
        {
        }

        Square(Square&& other) noexcept :
            Shape(other),
            _resource(std::move(other._resource))
        {
        }

        int sides() const override { return 4; }

        int id() const { return _resource.id(); }

    private:
        Tracked _resource;
    };

    void moveOnto(Holder<Shape>& target, Holder<Shape>& source)
    {
        target = std::move(source);
    }
}

TEST(Holder, DefaultHolderIsFalseAndHoldsNothing)
{
    Holder<Shape> holder;

    EXPECT_FALSE(static_cast<bool>(holder));
}

TEST(Holder, MakeConstructsTheDerivedAndReachesItThroughTheBase)
{
    Ledger ledger;

    Holder<Shape> holder = Holder<Shape>::make<Triangle>(ledger, 11);

    ASSERT_TRUE(static_cast<bool>(holder));
    EXPECT_EQ(3, holder->sides());
    EXPECT_EQ(3, (*holder).sides());
    EXPECT_EQ(1, ledger.live());
}

TEST(Holder, DestructionRunsTheDerivedDestructorExactlyOnce)
{
    Ledger ledger;

    {
        Holder<Shape> holder = Holder<Shape>::make<Square>(ledger, 12);
        ASSERT_EQ(1, ledger.live());
    }

    EXPECT_EQ(0, ledger.live()) << "the held object was not destroyed";
    EXPECT_EQ(1, ledger.destructor_runs) << "it was destroyed more than once";
}

TEST(Holder, MoveConstructionLeavesTheSourceEmptyAndKeepsOneLiveObject)
{
    Ledger ledger;

    {
        Holder<Shape> source = Holder<Shape>::make<Triangle>(ledger, 13);
        Holder<Shape> moved(std::move(source));

        EXPECT_TRUE(static_cast<bool>(moved));
        EXPECT_EQ(3, moved->sides());

        EXPECT_FALSE(static_cast<bool>(source));

        EXPECT_EQ(1, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
    EXPECT_EQ(1, ledger.destructor_runs);
}

TEST(Holder, MoveConstructionRecomputesTheBasePointerForTheNewStorage)
{
    Ledger ledger;

    Holder<Shape> source = Holder<Shape>::make<Square>(ledger, 14);
    Shape* const  before = &(*source);

    Holder<Shape> moved(std::move(source));

    EXPECT_NE(before, &(*moved));
    EXPECT_EQ(4, moved->sides());
}

TEST(Holder, MoveAssignmentDestroysWhatTheTargetWasHolding)
{
    Ledger ledger;

    {
        Holder<Shape> target = Holder<Shape>::make<Triangle>(ledger, 15);
        Holder<Shape> source = Holder<Shape>::make<Square>(ledger, 16);

        ASSERT_EQ(2, ledger.live());

        target = std::move(source);

        EXPECT_EQ(1, ledger.live());
        EXPECT_EQ(4, target->sides());
        EXPECT_FALSE(static_cast<bool>(source));
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Holder, MoveAssignmentOntoAnEmptyHolderJustTakesTheObject)
{
    Ledger ledger;

    {
        Holder<Shape> target;
        Holder<Shape> source = Holder<Shape>::make<Square>(ledger, 17);

        target = std::move(source);

        EXPECT_TRUE(static_cast<bool>(target));
        EXPECT_EQ(1, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
}

TEST(Holder, MoveAssignmentFromAnEmptyHolderEmptiesTheTarget)
{
    Ledger ledger;

    Holder<Shape> target = Holder<Shape>::make<Triangle>(ledger, 18);
    Holder<Shape> source;

    target = std::move(source);

    EXPECT_FALSE(static_cast<bool>(target));
    EXPECT_EQ(0, ledger.live());
}

TEST(Holder, SelfMoveAssignmentKeepsTheObject)
{
    Ledger ledger;

    Holder<Shape> holder = Holder<Shape>::make<Square>(ledger, 19);

    moveOnto(holder, holder);

    EXPECT_TRUE(static_cast<bool>(holder));
    EXPECT_EQ(4, holder->sides());
    EXPECT_EQ(1, ledger.live());
}

TEST(Holder, ResetDestroysTheObjectAndIsIdempotent)
{
    Ledger ledger;

    Holder<Shape> holder = Holder<Shape>::make<Triangle>(ledger, 20);

    holder.reset();

    EXPECT_FALSE(static_cast<bool>(holder));
    EXPECT_EQ(0, ledger.live());
    EXPECT_EQ(1, ledger.destructor_runs);

    holder.reset();

    EXPECT_FALSE(static_cast<bool>(holder));
    EXPECT_EQ(1, ledger.destructor_runs);
}

TEST(Holder, ResetOnADefaultHolderDoesNothing)
{
    Ledger        ledger;
    Holder<Shape> holder;

    holder.reset();

    EXPECT_FALSE(static_cast<bool>(holder));
    EXPECT_EQ(0, ledger.destructor_runs);
}

TEST(Holder, DifferentDerivedTypesEachGetTheirOwnDestructor)
{
    Ledger ledger;

    {
        Holder<Shape> triangle = Holder<Shape>::make<Triangle>(ledger, 21);
        Holder<Shape> square   = Holder<Shape>::make<Square>(ledger, 22);

        EXPECT_EQ(3, triangle->sides());
        EXPECT_EQ(4, square->sides());
        EXPECT_EQ(2, ledger.live());
    }

    EXPECT_EQ(0, ledger.live());
    EXPECT_EQ(2, ledger.destructor_runs);
}
