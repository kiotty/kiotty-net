#pragma once

#include "http/data/state/kiotty_http_session.h"
#include "http/domain/entity/kiotty_http_answer.h"

namespace kiotty::net::implementation
{
    void writeAnswer(HttpSession& session, const Answer& answer);
}
