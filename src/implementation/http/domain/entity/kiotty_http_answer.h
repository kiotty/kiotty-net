#pragma once

#include "core/kiotty_fixed_string.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using AnswerPath = FixedString<KIOTTY_NET_MAX_PATH_LENGTH>;

    enum class AnswerKind : uint8_t
    {
        NONE,
        STATUS,
        ALLOW,
        MEMORY,
        FILE
    };

    struct Answer
    {
        AnswerPath               location;
        KiottyNetSpan            memory;
        KiottyNetHttpContentType type;
        uint32_t                 allowed;
        int32_t                  status;
        AnswerKind               kind;
    };

    struct RouteOutcome
    {
        bool is_stream;
        bool is_head;
        bool is_work_requested;
    };

    enum class AfterRoute : uint8_t
    {
        SEND,
        AWAIT_WORK,
        REFUSE_STREAM
    };
}
