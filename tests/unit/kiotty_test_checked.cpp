#include "core/kiotty_checked.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>

using kiotty::net::implementation::checkedAdd;
using kiotty::net::implementation::checkedMultiply;

TEST(Checked, AddReturnsTheSumWhenItFits)
{
    const auto sum = checkedAdd<size_t>(40, 2);
    ASSERT_TRUE(sum);
    EXPECT_EQ(sum.value, 42u);
}

TEST(Checked, AddReachesTheMaximumExactly)
{
    const uint32_t maximum = std::numeric_limits<uint32_t>::max();
    const auto sum = checkedAdd<uint32_t>(maximum - 1u, 1u);
    ASSERT_TRUE(sum);
    EXPECT_EQ(sum.value, maximum);
}

TEST(Checked, AddRefusesAnOverflow)
{
    const size_t maximum = std::numeric_limits<size_t>::max();
    const auto sum = checkedAdd<size_t>(maximum, 1);
    EXPECT_FALSE(sum);
    EXPECT_EQ(sum.code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
}

TEST(Checked, MultiplyReturnsTheProductWhenItFits)
{
    const auto product = checkedMultiply<size_t>(6, 7);
    ASSERT_TRUE(product);
    EXPECT_EQ(product.value, 42u);
}

TEST(Checked, MultiplyByZeroIsZero)
{
    const size_t maximum = std::numeric_limits<size_t>::max();
    const auto left = checkedMultiply<size_t>(0, maximum);
    const auto right = checkedMultiply<size_t>(maximum, 0);
    ASSERT_TRUE(left);
    ASSERT_TRUE(right);
    EXPECT_EQ(left.value, 0u);
    EXPECT_EQ(right.value, 0u);
}

TEST(Checked, MultiplyRefusesAnOverflow)
{
    const uint32_t half = std::numeric_limits<uint32_t>::max() / 2u + 1u;
    const auto product = checkedMultiply<uint32_t>(half, 2u);
    EXPECT_FALSE(product);
    EXPECT_EQ(product.code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
}
