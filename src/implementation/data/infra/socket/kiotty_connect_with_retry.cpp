#include "data/infra/socket/kiotty_connect_with_retry.h"

#include "data/infra/socket/kiotty_socket_transfer.h"

#include <chrono>
#include <thread>
#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        const std::chrono::milliseconds STOP_CHECK(10);

        bool               waitInterval(uint32_t interval_milliseconds, const std::atomic<bool>& stop);
        ActiveSocketResult tryOnce(const KiottyNetClientConfig& config);
        NoDelay            noDelayOf(const KiottyNetClientConfig& config);
    }

    ConnectedResult connectWithRetry(const KiottyNetClientConfig& config, const std::atomic<bool>& stop)
    {
        const uint32_t trials = config.max_trial == 0 ? 1 : config.max_trial;
        SocketCode last = SocketCode::UNKNOWN;
        for (uint32_t trial = 0; trial < trials; ++trial)
        {
            const bool waited = trial == 0 || waitInterval(config.trial_interval_ms, stop);
            if (!waited) { return ConnectedResult::failure(KIOTTY_CLOSED); }
            if (stop.load()) { return ConnectedResult::failure(KIOTTY_CLOSED); }
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
            using Clock = std::chrono::steady_clock;
            const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(interval_milliseconds);
            for (;;)
            {
                if (stop.load()) { return false; }
                const Clock::time_point now = Clock::now();
                if (now >= deadline) { return true; }
                const Clock::duration remaining = deadline - now;
                const Clock::duration slice = remaining < STOP_CHECK ? remaining : Clock::duration(STOP_CHECK);
                std::this_thread::sleep_for(slice);
            }
        }

        ActiveSocketResult tryOnce(const KiottyNetClientConfig& config)
        {
            ActiveSocketResult opened = ActiveSocket::open(config.endpoint.transport);
            if (!opened) { return opened; }
            ActiveSocket& socket = opened.value;
            const SocketCode connected = socket.connect(config.endpoint, config.connect_timeout_ms);
            if (connected != SocketCode::SUCCESS) { return ActiveSocketResult::failure(connected); }
            const SocketCode tuned = configureNoDelay(socket, noDelayOf(config));
            if (tuned != SocketCode::SUCCESS) { return ActiveSocketResult::failure(tuned); }
            return ActiveSocketResult::success(std::move(socket));
        }

        NoDelay noDelayOf(const KiottyNetClientConfig& config)
        {
            return config.no_delay ? NoDelay::ENABLED : NoDelay::DISABLED;
        }
    }
}
