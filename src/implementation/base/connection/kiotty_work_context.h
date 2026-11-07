#if !defined(KIOTTY_WORK_CONTEXT_H)
#define KIOTTY_WORK_CONTEXT_H

#include "domain/entity/kiotty_io_action.h"
#include "kiotty_net_base.h"

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
        bool     requested() const;
        bool     finished() const;
        IoAction onFinished();
        bool     working() const;
        void     reset();

        KiottyNetCode verify(int32_t generation) const;
        void          finish(IoAction outcome);

    private:
        void move(State from, State to);

        std::atomic<State> _state;
        int32_t            _generation;
        IoAction           _outcome;
    };

    inline WorkContext::WorkContext()
        : _state(State::IDLE)
        , _generation(0)
        , _outcome(IoAction::none())
    {
    }

    inline void WorkContext::move(State from, State to)
    {
        State expected = from;
        _state.compare_exchange_strong(expected, to, std::memory_order_acq_rel, std::memory_order_acquire);
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
        if (_state.load(std::memory_order_acquire) != State::REQUESTED)
        {
            return;
        }
        _generation = generation;
        _state.store(State::WORKING, std::memory_order_release);
    }

    inline bool WorkContext::requested() const
    {
        return _state.load(std::memory_order_acquire) == State::REQUESTED;
    }

    inline bool WorkContext::finished() const
    {
        return _state.load(std::memory_order_acquire) == State::DONE;
    }

    inline IoAction WorkContext::onFinished()
    {
        if (!finished())
        {
            return IoAction::none();
        }
        IoAction outcome = _outcome;
        _outcome = IoAction::none();
        _state.store(State::IDLE, std::memory_order_release);
        return outcome;
    }

    inline bool WorkContext::working() const
    {
        return _state.load(std::memory_order_acquire) != State::IDLE;
    }

    inline void WorkContext::reset()
    {
        _generation = 0;
        _outcome = IoAction::none();
        _state.store(State::IDLE, std::memory_order_release);
    }

    inline KiottyNetCode WorkContext::verify(int32_t generation) const
    {
        if (_state.load(std::memory_order_acquire) != State::WORKING)
        {
            return KIOTTY_NET_CODE_BUSY;
        }
        if (_generation != generation)
        {
            return KIOTTY_NET_CODE_PEER_CLOSED;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    inline void WorkContext::finish(IoAction outcome)
    {
        _outcome = outcome;
        move(State::WORKING, State::DONE);
    }
}

#endif
