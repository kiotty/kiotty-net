#pragma once

#include "http/data/repository/kiotty_http_registry.h"
#include "http/data/repository/kiotty_http_session.h"

namespace kiotty::net::implementation
{
    void answerStatic(HttpSession& session, const StaticTarget& target);
}
