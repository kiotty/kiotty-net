#include "domain/usecases/io_loop/kiotty_io_loop.h"

#include "core/kiotty_clock.h"
#include "data/repository/connection/kiotty_connection.h"
#include "data/repository/connection/kiotty_connection_status.h"
#include "domain/entity/kiotty_phase.h"

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        uint32_t nearer(uint32_t timeout, uint64_t deadline, uint64_t now);
    }

    IoLoop::IoLoop()
        : _stopped(false)
        , _io_thread()
        , _accept_retry_at(0)
    {
    }

    KiottyNetCode IoLoop::run(const LoopParts& parts, StopPolicy policy)
    {
        _io_thread.store(std::this_thread::get_id());
        if (parts.acceptor.listening())
        {
            armAccept(parts);
        }
        stepUntilQuiet(parts);
        while (!_stopped.load())
        {
            runOnce(parts, policy);
        }
        shutdown(parts);
        _io_thread.store(std::thread::id());
        _stopped.store(false);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void IoLoop::stop()
    {
        _stopped.store(true);
    }

    bool IoLoop::isIoThread() const
    {
        return _io_thread.load() == std::this_thread::get_id();
    }

    void IoLoop::admit(const LoopParts& parts, ActiveSocket&& socket, const char* hostname)
    {
        welcome(std::move(socket), hostname, parts);
    }

    void IoLoop::runOnce(const LoopParts& parts, StopPolicy policy)
    {
        drain(parts);
        settle(parts, policy);
        const Span<const IoCompletion> arrived = collectIoEvents(parts);
        for (size_t i = 0; i < arrived.length; ++i)
        {
            step(arrived.data[i], parts);
        }
        stepUntilQuiet(parts);
    }

    void IoLoop::drain(const LoopParts& parts)
    {
        for (int32_t i = 0; i < parts.host.capacity(); ++i)
        {
            Connection& connection = parts.host.at(i);
            const IoAction write = connection.flows().send.pick(parts.host.notificationMutex());
            submitTo(connection, write, parts);
        }
    }

    void IoLoop::settle(const LoopParts& parts, StopPolicy policy)
    {
        releaseSettled(parts);
        rearmAccept(parts);
        const bool stop_when_empty = policy == StopPolicy::WHEN_EMPTY;
        if (stop_when_empty && parts.host.empty())
        {
            _stopped.store(true);
        }
    }

    void IoLoop::releaseSettled(const LoopParts& parts)
    {
        for (int32_t i = 0; i < parts.host.capacity(); ++i)
        {
            Connection& connection = parts.host.at(i);
            const ConnectionStatus status = connection.status();
            const bool is_closing = status.phase == Phase::CLOSING;
            if (is_closing && status.isSettled())
            {
                parts.host.release(connection);
            }
        }
    }

    void IoLoop::rearmAccept(const LoopParts& parts)
    {
        if (!parts.acceptor.listening())
        {
            return;
        }
        if (_stopped.load() || parts.acceptor.isPending() || parts.host.full())
        {
            return;
        }
        if (monotonicMilliseconds() < _accept_retry_at)
        {
            return;
        }
        armAccept(parts);
    }

    Span<const IoCompletion> IoLoop::collectIoEvents(const LoopParts& parts)
    {
        const uint32_t timeout = waitTimeout(parts, monotonicMilliseconds());
        return parts.listener.wait(timeout);
    }

    uint32_t IoLoop::waitTimeout(const LoopParts& parts, uint64_t now)
    {
        const bool has_nothing_left = _stopped.load() && parts.host.empty()
                                      && !parts.acceptor.isPending();
        if (has_nothing_left)
        {
            return 0;
        }
        uint32_t timeout = EventListener::WAIT_FOREVER;
        for (int32_t i = 0; i < parts.host.capacity(); ++i)
        {
            timeout = nearer(timeout, parts.host.at(i).status().deadline, now);
        }
        if (_accept_retry_at > now)
        {
            timeout = nearer(timeout, _accept_retry_at, now);
        }
        return timeout;
    }

    void IoLoop::shutdown(const LoopParts& parts)
    {
        for (int32_t i = 0; i < parts.host.capacity(); ++i)
        {
            Connection& connection = parts.host.at(i);
            submitTo(connection, connection.close(KIOTTY_NET_CODE_CLOSED, parts.handler), parts);
        }
        if (parts.acceptor.listening())
        {
            parts.acceptor.detach(parts.listener);
            parts.acceptor.shutdown();
        }
        while (!parts.host.empty() || parts.acceptor.isPending())
        {
            runOnce(parts, StopPolicy::WHEN_STOPPED);
        }
    }

    namespace
    {
        uint32_t nearer(uint32_t timeout, uint64_t deadline, uint64_t now)
        {
            if (deadline == 0)
            {
                return timeout;
            }
            const uint64_t remaining = deadline > now ? deadline - now : 0;
            if (remaining >= timeout)
            {
                return timeout;
            }
            return static_cast<uint32_t>(remaining);
        }
    }
}
