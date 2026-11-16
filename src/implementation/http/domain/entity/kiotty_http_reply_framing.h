#pragma once

#include "http/domain/entity/kiotty_http_message_framing.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    enum class ReplyBodyKind : uint8_t
    {
        NONE,
        LENGTH,
        CHUNKED,
        UNTIL_CLOSE
    };

    enum class ReplyPhase : uint8_t
    {
        FINAL,
        INTERIM
    };

    struct ReplyFraming
    {
        uint64_t      length;
        ReplyBodyKind kind;
        Persistence   persistence;
        ReplyPhase    phase;
    };

    static_assert(sizeof(ReplyFraming) == 2 * sizeof(uint64_t), "ReplyFraming: unexpected padding");
}
