#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/response/kiotty_http_response_store.h"
#include "http/domain/entity/kiotty_http_message_framing.h"
#include "http/domain/entity/kiotty_http_send_step.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    SendStep      sendStepOf(const ResponseFrame& frame,
                             size_t sent,
                             Span<const char> chunk,
                             Persistence persistence);
    AfterResponse afterResponseOf(Persistence persistence);
}
