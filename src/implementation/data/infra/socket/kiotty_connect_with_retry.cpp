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

        bool               waitInterval(uint32_t interval_milliseconds,
                               const std::atomic<bool>& stop);
        ActiveSocketResult tryOnce(const KiottyNetClientConfiguration& config);
        NoDelay            noDelayOf(const KiottyNetClientConfiguration& config);
    }

    ConnectedResult connectWithRetry(const KiottyNetClientConfiguration& config,
        const std::atomic<bool>& stop)
    {
        const uint32_t trials = config.max_trial == 0 ? 1 : config.max_trial;
        const uint32_t interval_milliseconds = config.trial_interval_milliseconds;
        SocketCode last = SocketCode::UNKNOWN;
        for (uint32_t trial = 0; trial < trials; ++trial)
        {
            const bool waited = trial == 0 || waitInterval(interval_milliseconds, stop);
            if (!waited) { return ConnectedResult::failure(KIOTTY_NET_CODE_CLOSED); }
            if (stop.load()) { return ConnectedResult::failure(KIOTTY_NET_CODE_CLOSED); }
            ActiveSocketResult attempt = tryOnce(config);
            if (attempt) { return ConnectedResult::success(std::move(attempt.value)); }
            last = attempt.code;
        }
        return ConnectedResult::failure(toNetCode(last));
    }

    namespace
    {
        bool waitInterval(uint32_t interval_milliseconds, const std::atomic<bool>& stop)
        {
            const std::chrono::milliseconds interval(interval_milliseconds);
            const Clock::time_point deadline = Clock::now() + interval;
            const Clock::duration stop_check = STOP_CHECK;
            for (;;)
            {
                if (stop.load()) { return false; }
                const Clock::time_point now = Clock::now();
                if (now >= deadline) { return true; }
                const Clock::duration remaining = deadline - now;
                const Clock::duration slice = std::min(remaining, stop_check);
                std::this_thread::sleep_for(slice);
            }
        }

        ActiveSocketResult tryOnce(const KiottyNetClientConfiguration& config)
        {
            ActiveSocketResult opened = ActiveSocket::open(config.address.transport);
            if (!opened) { return opened; }
            ActiveSocket& socket = opened.value;
            const uint32_t timeout_milliseconds = config.connect_timeout_milliseconds;
            const SocketCode connected = socket.connect(config.address, timeout_milliseconds);
            if (connected != SocketCode::SUCCESS) { return ActiveSocketResult::failure(connected); }
            const SocketCode tuned = configureNoDelay(socket, noDelayOf(config));
            if (tuned != SocketCode::SUCCESS) { return ActiveSocketResult::failure(tuned); }
            return ActiveSocketResult::success(std::move(socket));
        }

        NoDelay noDelayOf(const KiottyNetClientConfiguration& config)
        {
            return config.no_delay ? NoDelay::ENABLED : NoDelay::DISABLED;
        }
    }
}
