#include "health_check_service.h"

namespace health_check
{
    namespace
    {
        std::string startEvents(ServiceState& state, Status& status)
        {
            status = STATUS_OK;
            if (state.events_on)
            {
                return "events already started";
            }
            state.events_on  = true;
            state.event_seq  = 0;
            state.next_event = Clock::now() + std::chrono::milliseconds(kEventIntervalMs);
            return "events started, every " + std::to_string(kEventIntervalMs) + " ms";
        }

        std::string stopEvents(ServiceState& state, Status& status)
        {
            status = STATUS_OK;
            if (!state.events_on)
            {
                return "events already stopped";
            }
            state.events_on = false;
            return "events stopped after " + std::to_string(state.event_seq) + " events";
        }

        std::string openCursor(ServiceState& state, Status& status)
        {
            if (state.cursor_open)
            {
                status = STATUS_ERROR;
                return "cursor already open";
            }
            status                = STATUS_OK;
            state.cursor_open     = true;
            state.cursor_position = 0;
            return "cursor opened, " + std::to_string(kCursorRows) + " rows";
        }

        std::string fetchCursor(ServiceState& state, Status& status)
        {
            if (!state.cursor_open)
            {
                status = STATUS_ERROR;
                return "cursor not open";
            }
            if (state.cursor_position >= kCursorRows)
            {
                status = STATUS_END;
                return "no more rows";
            }
            status         = STATUS_OK;
            uint32_t first = state.cursor_position + 1;
            uint32_t last  = state.cursor_position + kCursorRowsPerFetch;
            if (last > kCursorRows)
            {
                last = kCursorRows;
            }
            std::string body = "rows " + std::to_string(first) + "-" + std::to_string(last) + " of " + std::to_string(kCursorRows);
            for (uint32_t row = first; row <= last; ++row)
            {
                body += "\n  row " + std::to_string(row) + " value=" + std::to_string(row * row);
            }
            state.cursor_position = last;
            return body;
        }

        std::string closeCursor(ServiceState& state, Status& status)
        {
            if (!state.cursor_open)
            {
                status = STATUS_ERROR;
                return "cursor not open";
            }
            status            = STATUS_OK;
            state.cursor_open = false;
            return "cursor closed at row " + std::to_string(state.cursor_position);
        }

        std::string single(const ServiceState& state, const std::string& request_body, const ServerStats& stats, Status& status)
        {
            status           = STATUS_OK;
            std::string body = "status=UP uptime_ms=" + std::to_string(stats.uptime_ms);
            body += " connections=" + std::to_string(stats.connections);
            body += state.events_on ? " events=on" : " events=off";
            body += state.cursor_open ? " cursor=open" : " cursor=closed";
            if (!request_body.empty())
            {
                body += " echo=" + request_body;
            }
            return body;
        }
    }

    void resetService(ServiceState& state)
    {
        state.events_on       = false;
        state.event_seq       = 0;
        state.next_event      = Clock::now();
        state.cursor_open     = false;
        state.cursor_position = 0;
    }

    std::string answerRequest(ServiceState& state, const Header& header, const std::string& body, const ServerStats& stats, Status& status)
    {
        switch (header.command)
        {
        case COMMAND_START_EVENTS:
            return startEvents(state, status);
        case COMMAND_STOP_EVENTS:
            return stopEvents(state, status);
        case COMMAND_OPEN_CURSOR:
            return openCursor(state, status);
        case COMMAND_FETCH_CURSOR:
            return fetchCursor(state, status);
        case COMMAND_CLOSE_CURSOR:
            return closeCursor(state, status);
        case COMMAND_SINGLE:
            return single(state, body, stats, status);
        default:
            status = STATUS_ERROR;
            return "unknown command";
        }
    }

    bool takeDueEvent(ServiceState& state, Clock::time_point now, const ServerStats& stats, std::string& body)
    {
        if (!state.events_on)
        {
            return false;
        }
        if (now < state.next_event)
        {
            return false;
        }
        state.event_seq += 1;
        body = "seq=" + std::to_string(state.event_seq);
        body += " uptime_ms=" + std::to_string(stats.uptime_ms);
        body += " connections=" + std::to_string(stats.connections);
        state.next_event += std::chrono::milliseconds(kEventIntervalMs);
        if (state.next_event < now)
        {
            state.next_event = now + std::chrono::milliseconds(kEventIntervalMs);
        }
        return true;
    }

    uint32_t millisecondsUntilEvent(const ServiceState& state, Clock::time_point now)
    {
        if (!state.events_on)
        {
            return 0;
        }
        if (now >= state.next_event)
        {
            return 1;
        }
        int64_t left = elapsedMilliseconds(now, state.next_event);
        if (left < 1)
        {
            return 1;
        }
        return static_cast<uint32_t>(left);
    }

    int64_t elapsedMilliseconds(Clock::time_point from, Clock::time_point to)
    {
        std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(to - from);
        return static_cast<int64_t>(elapsed.count());
    }
}
