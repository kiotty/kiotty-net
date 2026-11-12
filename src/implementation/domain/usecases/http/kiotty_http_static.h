#pragma once

#include "data/repository/http/kiotty_http_registry.h"
#include "data/repository/http/kiotty_http_session.h"

namespace kiotty::net::implementation
{
    void answerStatic(HttpSession& session, const StaticTarget& target);
}
