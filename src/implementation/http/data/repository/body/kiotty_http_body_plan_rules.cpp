#include "http/data/repository/body/kiotty_http_body_plan_rules.h"

#include "http/domain/entity/kiotty_http_constants.h"

namespace kiotty::net::implementation
{
    BodyPlanResult planBody(uint64_t length, uint64_t room)
    {
        const bool is_too_large = length > room;
        if (is_too_large) { return BodyPlanResult::failure(CONTENT_TOO_LARGE); }
        return BodyPlanResult::success(BodyPlan{ length });
    }

    BodyPlan takeBodyPiece(const BodyPlan& plan, size_t length)
    {
        return BodyPlan{ plan.remaining - length };
    }

    size_t nextReadOf(const BodyPlan& plan, size_t capacity)
    {
        const bool is_short = plan.remaining < capacity;
        return is_short ? static_cast<size_t>(plan.remaining) : capacity;
    }

    bool hasMoreBody(const BodyPlan& plan)
    {
        return plan.remaining > 0;
    }
}
