#pragma once

#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_answer.h"
#include "http/domain/entity/kiotty_http_route_match.h"

namespace kiotty::net::implementation
{
    Answer     answerOf(const RouteMatch& match, Span<const char> path);
    AfterRoute afterRouteOf(const RouteOutcome& outcome);
}
