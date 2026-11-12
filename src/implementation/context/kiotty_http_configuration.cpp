#include "context/kiotty_http_configuration.h"

#include "context/kiotty_configuration.h"

namespace kiotty::net::implementation
{
    namespace
    {
        struct SizeRange
        {
            size_t minimum;
            size_t maximum;
        };

        constexpr size_t   KIBIBYTE = 1024;
        constexpr size_t   MEBIBYTE = 1024 * KIBIBYTE;
        constexpr size_t   DEFAULT_MAX_ROUTES = 64;
        constexpr size_t   DEFAULT_MAX_HEADER_BYTES = 8 * KIBIBYTE;
        constexpr size_t   DEFAULT_MAX_HEADERS = 32;
        constexpr size_t   DEFAULT_MAX_BODY_BYTES = 64 * KIBIBYTE;
        constexpr size_t   DEFAULT_RESPONSE_BYTES = 64 * KIBIBYTE;
        constexpr uint32_t DEFAULT_HEADER_TIMEOUT_MILLISECONDS = 10000;
        constexpr uint32_t DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS = 15000;
        constexpr uint32_t DEFAULT_PING_INTERVAL_MILLISECONDS = 30000;
        constexpr uint32_t DEFAULT_PONG_TIMEOUT_MILLISECONDS = 10000;
        constexpr uint32_t DEFAULT_CLOSE_TIMEOUT_MILLISECONDS = 5000;
        constexpr size_t   MIN_HEADER_BYTES = 64;

        constexpr SizeRange ROUTES{ 1, 4096 };
        constexpr SizeRange HEADER_BYTES{ MIN_HEADER_BYTES, MEBIBYTE };
        constexpr SizeRange HEADERS{ 1, 1024 };
        constexpr SizeRange BODY_BYTES{ 0, 64 * MEBIBYTE };
        constexpr size_t    MIN_RESPONSE_BYTES = RESPONSE_PREFIX_BYTES + MIN_HEADER_BYTES;
        constexpr SizeRange RESPONSE_BYTES{ MIN_RESPONSE_BYTES, 64 * MEBIBYTE };

        bool isWithin(size_t value, SizeRange range);
    }

    KiottyNetCode initializeHttpLimits(KiottyNetHttpLimits& limits)
    {
        limits = KiottyNetHttpLimits{};
        limits.initialization_mark = INITIALIZATION_MARK;
        limits.max_routes = DEFAULT_MAX_ROUTES;
        limits.max_header_bytes = DEFAULT_MAX_HEADER_BYTES;
        limits.max_headers = DEFAULT_MAX_HEADERS;
        limits.max_body_bytes = DEFAULT_MAX_BODY_BYTES;
        limits.response_bytes = DEFAULT_RESPONSE_BYTES;
        limits.header_timeout_milliseconds = DEFAULT_HEADER_TIMEOUT_MILLISECONDS;
        limits.keep_alive_timeout_milliseconds = DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS;
        limits.websocket_ping_interval_milliseconds = DEFAULT_PING_INTERVAL_MILLISECONDS;
        limits.websocket_pong_timeout_milliseconds = DEFAULT_PONG_TIMEOUT_MILLISECONDS;
        limits.websocket_close_timeout_milliseconds = DEFAULT_CLOSE_TIMEOUT_MILLISECONDS;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureHttpMaxRoutes(KiottyNetHttpLimits& limits, size_t max_routes)
    {
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isWithin(max_routes, ROUTES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        limits.max_routes = max_routes;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureHttpHeaderCapacity(KiottyNetHttpLimits& limits,
                                              size_t max_header_bytes,
                                              size_t max_headers)
    {
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isWithin(max_header_bytes, HEADER_BYTES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isWithin(max_headers, HEADERS)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        limits.max_header_bytes = max_header_bytes;
        limits.max_headers = max_headers;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureHttpBodyCapacity(KiottyNetHttpLimits& limits, size_t max_body_bytes)
    {
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isWithin(max_body_bytes, BODY_BYTES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        limits.max_body_bytes = max_body_bytes;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureHttpResponseCapacity(KiottyNetHttpLimits& limits,
                                                size_t response_bytes)
    {
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        if (!isWithin(response_bytes, RESPONSE_BYTES)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        limits.response_bytes = response_bytes;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureHttpTimeouts(KiottyNetHttpLimits& limits,
                                        uint32_t header_timeout_milliseconds,
                                        uint32_t keep_alive_timeout_milliseconds)
    {
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        limits.header_timeout_milliseconds = header_timeout_milliseconds;
        limits.keep_alive_timeout_milliseconds = keep_alive_timeout_milliseconds;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode configureHttpWebSocketTimers(KiottyNetHttpLimits& limits,
                                               uint32_t ping_interval_milliseconds,
                                               uint32_t pong_timeout_milliseconds,
                                               uint32_t close_timeout_milliseconds)
    {
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        limits.websocket_ping_interval_milliseconds = ping_interval_milliseconds;
        limits.websocket_pong_timeout_milliseconds = pong_timeout_milliseconds;
        limits.websocket_close_timeout_milliseconds = close_timeout_milliseconds;
        return KIOTTY_NET_CODE_SUCCESS;
    }

    bool isInitialized(const KiottyNetHttpLimits& limits)
    {
        return limits.initialization_mark == INITIALIZATION_MARK;
    }

    namespace
    {
        bool isWithin(size_t value, SizeRange range)
        {
            return value >= range.minimum && value <= range.maximum;
        }
    }
}
