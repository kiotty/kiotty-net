#pragma once

#include "domain/usecases/http/kiotty_http_response_text.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    struct HttpServerConfiguration
    {
        KiottyNetServerConfiguration server;
        KiottyNetHttpLimits          limits;
    };

    KiottyNetCode initializeHttpLimits(KiottyNetHttpLimits& limits);
    KiottyNetCode configureHttpMaxRoutes(KiottyNetHttpLimits& limits, size_t max_routes);
    KiottyNetCode configureHttpHeaderCapacity(KiottyNetHttpLimits& limits,
                                              size_t max_header_bytes,
                                              size_t max_headers);
    KiottyNetCode configureHttpBodyCapacity(KiottyNetHttpLimits& limits, size_t max_body_bytes);
    KiottyNetCode configureHttpResponseCapacity(KiottyNetHttpLimits& limits,
                                                size_t response_bytes);
    KiottyNetCode configureHttpTimeouts(KiottyNetHttpLimits& limits,
                                        uint32_t header_timeout_milliseconds,
                                        uint32_t keep_alive_timeout_milliseconds);
    KiottyNetCode configureHttpWebSocketTimers(KiottyNetHttpLimits& limits,
                                               uint32_t ping_interval_milliseconds,
                                               uint32_t pong_timeout_milliseconds,
                                               uint32_t close_timeout_milliseconds);
    bool          isInitialized(const KiottyNetHttpLimits& limits);
}
