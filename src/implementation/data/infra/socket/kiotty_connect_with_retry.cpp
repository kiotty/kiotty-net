#include "data/infra/socket/kiotty_connect_with_retry.h"

#include "data/infra/socket/kiotty_socket_transfer.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        const std::chrono::milliseconds STOP_CHECK(10);

        struct Halt
        {
            const std::atomic<bool>* flag;
            const StopSignal*        signal;
        };

        ConnectedResult    retry(const KiottyNetClientConfiguration& configuration,
                                 const KiottyNetAddress& address,
                                 const Halt& halt,
                                 const Deadline& deadline);
        bool               isHalted(const Halt& halt);
        uint32_t           pauseOf(uint32_t interval_milliseconds, const Deadline& deadline);
        bool               waitInterval(uint32_t interval_milliseconds, const Halt& halt);
        ActiveSocketResult tryOnce(const KiottyNetClientConfiguration& configuration,
                                   const KiottyNetAddress& address,
                                   const StopSignal* stop,
                                   uint32_t timeout_milliseconds);
        NoDelay            noDelayOf(const KiottyNetClientConfiguration& configuration);
    }

    ConnectedResult connectWithRetry(const KiottyNetClientConfiguration& configuration,
                                     const KiottyNetAddress& address,
                                     const std::atomic<bool>& stop)
    {
        return retry(configuration, address, Halt{ &stop, nullptr }, Deadline(0));
    }

    ConnectedResult connectWithRetry(const KiottyNetClientConfiguration& configuration,
                                     const KiottyNetAddress& address,
                                     const StopSignal& stop,
                                     const Deadline& deadline)
    {
        return retry(configuration, address, Halt{ nullptr, &stop }, deadline);
    }

    namespace
    {
        ConnectedResult retry(const KiottyNetClientConfiguration& configuration,
                              const KiottyNetAddress& address,
                              const Halt& halt,
                              const Deadline& deadline)
        {
            const uint32_t trials = configuration.max_trial == 0 ? 1 : configuration.max_trial;
            const uint32_t interval_milliseconds = configuration.trial_interval_milliseconds;
            const uint32_t connect_milliseconds = configuration.connect_timeout_milliseconds;
            SocketCode last = SocketCode::TIMEOUT;
            for (uint32_t trial = 0; trial < trials; ++trial)
            {
                const uint32_t pause = pauseOf(interval_milliseconds, deadline);
                const bool waited = trial == 0 || waitInterval(pause, halt);
                const bool is_stopped = !waited || isHalted(halt);
                if (is_stopped) { return ConnectedResult::failure(KIOTTY_NET_CODE_CLOSED); }
                if (deadline.expired()) { break; }
                const uint32_t timeout_milliseconds = deadline.bound(connect_milliseconds);
                ActiveSocketResult attempt =
                    tryOnce(configuration, address, halt.signal, timeout_milliseconds);
                if (attempt) { return ConnectedResult::success(std::move(attempt.value)); }
                last = attempt.code;
            }
            return ConnectedResult::failure(toNetCode(last));
        }

        bool isHalted(const Halt& halt)
        {
            const bool is_flagged = halt.flag != nullptr && halt.flag->load();
            const bool is_signaled = halt.signal != nullptr && halt.signal->isRaised();
            return is_flagged || is_signaled;
        }

        uint32_t pauseOf(uint32_t interval_milliseconds, const Deadline& deadline)
        {
            if (!deadline.limited()) { return interval_milliseconds; }
            return std::min(interval_milliseconds, deadline.remaining());
        }

        bool waitInterval(uint32_t interval_milliseconds, const Halt& halt)
        {
            const std::chrono::milliseconds interval(interval_milliseconds);
            const Clock::time_point deadline = Clock::now() + interval;
            const Clock::duration stop_check = STOP_CHECK;
            for (;;)
            {
                if (isHalted(halt)) { return false; }
                const Clock::time_point now = Clock::now();
                if (now >= deadline) { return true; }
                const Clock::duration remaining = deadline - now;
                const Clock::duration slice = std::min(remaining, stop_check);
                std::this_thread::sleep_for(slice);
            }
        }

        ActiveSocketResult tryOnce(const KiottyNetClientConfiguration& configuration,
                                   const KiottyNetAddress& address,
                                   const StopSignal* stop,
                                   uint32_t timeout_milliseconds)
        {
            ActiveSocketResult opened = ActiveSocket::open(address.transport, stop);
            if (!opened) { return opened; }
            ActiveSocket& socket = opened.value;
            const SocketCode connected = socket.connect(address, timeout_milliseconds);
            if (connected != SocketCode::SUCCESS) { return ActiveSocketResult::failure(connected); }
            const SocketCode tuned = configureNoDelay(socket, noDelayOf(configuration));
            if (tuned != SocketCode::SUCCESS) { return ActiveSocketResult::failure(tuned); }
            return ActiveSocketResult::success(std::move(socket));
        }

        NoDelay noDelayOf(const KiottyNetClientConfiguration& configuration)
        {
            if (configuration.no_delay == KIOTTY_NET_NO_DELAY_ON) { return NoDelay::ENABLED; }
            return NoDelay::DISABLED;
        }
    }
}
