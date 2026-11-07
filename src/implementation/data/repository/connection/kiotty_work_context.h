#pragma once

#include "domain/entity/kiotty_io_action.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    class WorkContext
    {
    public:
        enum class State : uint8_t
        {
            IDLE,
            REQUESTED,
            WORKING,
            DONE
        };

        WorkContext();
        WorkContext(const WorkContext&) = delete;
        WorkContext& operator=(const WorkContext&) = delete;

        void     request();
        void     abandon();
        void     start(int32_t generation);
        State    state() const;
        IoAction takeOutcome();
        void     reset();

    private:
        friend class WorkReporter;

        void move(State from, State to);

        IoAction           _outcome;
        int32_t            _generation;
        std::atomic<State> _state;
    };

    inline WorkContext::WorkContext()
        : _outcome(IoAction::none())
        , _generation(0)
        , _state(State::IDLE)
    {
    }

    inline void WorkContext::request()
    {
        move(State::IDLE, State::REQUESTED);
    }

    inline void WorkContext::abandon()
    {
        move(State::REQUESTED, State::IDLE);
    }

    inline void WorkContext::start(int32_t generation)
    {
        const State current = state();
        if (current != State::REQUESTED)
        {
            return;
        }
        _generation = generation;
        _state.store(State::WORKING, std::memory_order_release);
    }

    inline WorkContext::State WorkContext::state() const
    {
        return _state.load(std::memory_order_acquire);
    }

    inline IoAction WorkContext::takeOutcome()
    {
        const State current = state();
        if (current != State::DONE)
        {
            return IoAction::none();
        }
        const IoAction outcome = _outcome;
        _outcome = IoAction::none();
        _state.store(State::IDLE, std::memory_order_release);
        return outcome;
    }

    inline void WorkContext::reset()
    {
        _generation = 0;
        _outcome = IoAction::none();
        _state.store(State::IDLE, std::memory_order_release);
    }

    inline void WorkContext::move(State from, State to)
    {
        State expected = from;
        const std::memory_order success = std::memory_order_acq_rel;
        const std::memory_order failure = std::memory_order_acquire;
        _state.compare_exchange_strong(expected, to, success, failure);
    }
}
