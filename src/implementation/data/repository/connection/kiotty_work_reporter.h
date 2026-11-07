#pragma once

#include "data/repository/connection/kiotty_work_context.h"
#include "domain/entity/kiotty_io_action.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class WorkReporter
    {
    public:
        explicit WorkReporter(WorkContext& work);

        KiottyNetCode verify(int32_t generation) const;
        void          finish(IoAction outcome);

    private:
        WorkContext& _work;
    };

    inline WorkReporter::WorkReporter(WorkContext& work)
        : _work(work)
    {
    }

    inline KiottyNetCode WorkReporter::verify(int32_t generation) const
    {
        const WorkContext::State current = _work.state();
        if (current != WorkContext::State::WORKING) { return KIOTTY_NET_CODE_BUSY; }
        if (_work._generation != generation) { return KIOTTY_NET_CODE_PEER_CLOSED; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    inline void WorkReporter::finish(IoAction outcome)
    {
        _work._outcome = outcome;
        _work.move(WorkContext::State::WORKING, WorkContext::State::DONE);
    }
}
