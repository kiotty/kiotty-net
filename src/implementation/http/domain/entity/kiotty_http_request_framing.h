#pragma once

#include "http/domain/entity/kiotty_http_message_framing.h"

namespace kiotty::net::implementation
{
    struct RequestFraming
    {
        BodyFraming body;
        Persistence persistence;
        bool        expects_continue;
    };
}
