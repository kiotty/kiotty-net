#pragma once

#include "core/kiotty_result.h"
#include "kiotty_net_base.h"

#include <limits>
#include <type_traits>

namespace kiotty::net::implementation
{
    template <typename Integer>
    Result<Integer, KiottyNetCode> checkedAdd(Integer left, Integer right)
    {
        static_assert(std::is_unsigned<Integer>::value, "checkedAdd: unsigned integers only");
        using Checked = Result<Integer, KiottyNetCode>;
        const Integer room = std::numeric_limits<Integer>::max() - right;
        if (left > room) { return Checked::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return Checked::success(static_cast<Integer>(left + right));
    }

    template <typename Integer>
    Result<Integer, KiottyNetCode> checkedMultiply(Integer left, Integer right)
    {
        static_assert(std::is_unsigned<Integer>::value, "checkedMultiply: unsigned integers only");
        using Checked = Result<Integer, KiottyNetCode>;
        if (left == 0 || right == 0) { return Checked::success(Integer{ 0 }); }
        const Integer room = std::numeric_limits<Integer>::max() / right;
        if (left > room) { return Checked::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
        return Checked::success(static_cast<Integer>(left * right));
    }
}
