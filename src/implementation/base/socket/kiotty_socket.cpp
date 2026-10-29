#include "base/socket/kiotty_socket.h"

#include <chrono>
#include <thread>
#include <utility>

namespace kiotty
{
    namespace net
    {
        namespace
        {
            const std::chrono::milliseconds STOP_CHECK(10);

            bool waitInterval(uint32_t interval_ms, const std::atomic<bool>& stop)
            {
                std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
                for (;;)
                {
                    if (stop.load())
                    {
                        return false;
                    }
                    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                    if (now >= deadline)
                    {
                        return true;
                    }
                    std::chrono::steady_clock::duration remaining = deadline - now;
                    std::chrono::steady_clock::duration slice = remaining < STOP_CHECK ? remaining : std::chrono::steady_clock::duration(STOP_CHECK);
                    std::this_thread::sleep_for(slice);
                }
            }
        }

        Result<KiottyNetCode, ActiveSocket> connectWithRetry(const KiottyNetClientConfig& config, const std::atomic<bool>& stop)
        {
            typedef Result<KiottyNetCode, ActiveSocket> Connected;
            uint32_t trials = config.max_trial;
            if (trials == 0)
            {
                trials = 1;
            }
            SocketCode last = SocketCode::UNKNOWN;
            for (uint32_t trial = 0; trial < trials; ++trial)
            {
                if (trial > 0)
                {
                    bool waited = waitInterval(config.trial_interval_ms, stop);
                    if (!waited)
                    {
                        return error(KIOTTY_CLOSED);
                    }
                }
                if (stop.load())
                {
                    return error(KIOTTY_CLOSED);
                }
                ActiveSocketResult opened = ActiveSocket::open(config.endpoint.transport);
                if (!opened.isOk())
                {
                    last = opened.code();
                    continue;
                }
                ActiveSocket& socket = opened.value();
                SocketCode connected = socket.connect(config.endpoint, config.connect_timeout_ms);
                if (connected != SocketCode::SUCCESS)
                {
                    last = connected;
                    continue;
                }
                SocketCode tuned = socket.setNoDelay(config.no_delay);
                if (tuned != SocketCode::SUCCESS)
                {
                    last = tuned;
                    continue;
                }
                return Connected(Connected::Success(), std::move(socket));
            }
            return error(toNetCode(last));
        }
    }
}
