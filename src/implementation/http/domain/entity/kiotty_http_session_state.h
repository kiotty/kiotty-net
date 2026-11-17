#pragma once

#include "http/domain/entity/kiotty_http_body_plan.h"
#include "http/domain/entity/kiotty_http_message_framing.h"
#include "http/domain/entity/kiotty_http_request_framing.h"
#include "kiotty_net_base.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    enum class SessionPhase : uint8_t
    {
        READING,
        FILLING,
        WORKING,
        STREAMING,
        SENDING,
        UPGRADING,
        SOCKET
    };

    struct HttpSessionState
    {
        RequestFraming framing;
        BodyPlan       body;
        uint64_t       header_deadline;
        size_t         sent;
        KiottyNetCode  close_reason;
        KiottyNetCode  stream_failure;
        SessionPhase   phase;
        Persistence    persistence;
        bool           is_work_requested;
    };
}
