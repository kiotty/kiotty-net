#pragma once

#include "kiotty_net_base.h"

namespace kiotty::net::implementation
{
    struct Connected
    {
        KiottyNetCode code;
        bool          is_reused;
    };

    struct ReplyHead
    {
        KiottyNetCode code;
        bool          saw_bytes;
    };

    static_assert(sizeof(Connected) == 2 * sizeof(KiottyNetCode), "Connected: unexpected padding");
    static_assert(sizeof(ReplyHead) == 2 * sizeof(KiottyNetCode), "ReplyHead: unexpected padding");
}
