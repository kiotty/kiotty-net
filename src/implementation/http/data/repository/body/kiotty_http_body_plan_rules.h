#pragma once

#include "core/kiotty_result.h"
#include "http/domain/entity/kiotty_http_body_plan.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    using BodyPlanResult = Result<BodyPlan, KiottyNetHttpStatus>;

    BodyPlanResult planBody(uint64_t length, uint64_t room);
    BodyPlan       takeBodyPiece(const BodyPlan& plan, size_t length);
    size_t         nextReadOf(const BodyPlan& plan, size_t capacity);
    bool           hasMoreBody(const BodyPlan& plan);
}
