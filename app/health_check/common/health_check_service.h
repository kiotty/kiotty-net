#ifndef KIOTTY_HEALTH_CHECK_SERVICE_H
#define KIOTTY_HEALTH_CHECK_SERVICE_H

#include "health_check_protocol.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace health_check
{
    typedef std::chrono::steady_clock Clock;

    struct ServiceState
    {
        bool              events_on;
        uint32_t          event_seq;
        Clock::time_point next_event;
        bool              cursor_open;
        uint32_t          cursor_position;
    };

    struct ServerStats
    {
        int64_t uptime_ms;
        int     connections;
    };

    void        resetService(ServiceState& state);
    std::string answerRequest(ServiceState& state, const Header& header, const std::string& body, const ServerStats& stats, Status& status);
    bool        takeDueEvent(ServiceState& state, Clock::time_point now, const ServerStats& stats, std::string& body);
    uint32_t    millisecondsUntilEvent(const ServiceState& state, Clock::time_point now);
    int64_t     elapsedMilliseconds(Clock::time_point from, Clock::time_point to);
}

#endif
