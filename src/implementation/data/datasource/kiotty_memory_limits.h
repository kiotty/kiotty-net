#pragma once

#include "kiotty_memory_limits_configuration.h"

#include <cstddef>

namespace kiotty::net::implementation
{
    static constexpr size_t OWNERS_PER_TYPE_LIMIT = 16;
    static constexpr size_t CONNECTIONS_PER_SERVER_LIMIT = 1024;

    struct MemoryLimits
    {
        static constexpr size_t SERVERS = CONFIGURED_SERVERS;
        static constexpr size_t STREAM_CLIENTS = CONFIGURED_STREAM_CLIENTS;
        static constexpr size_t EVENT_CLIENTS = CONFIGURED_EVENT_CLIENTS;
        static constexpr size_t CONNECTIONS_PER_SERVER = CONFIGURED_CONNECTIONS_PER_SERVER;
        static constexpr size_t RECEIVE_CAPACITY = CONFIGURED_RECEIVE_CAPACITY;
        static constexpr size_t SEND_CAPACITY = CONFIGURED_SEND_CAPACITY;
        static constexpr size_t PUSH_CAPACITY = CONFIGURED_PUSH_CAPACITY;
        static constexpr bool   TLS = CONFIGURED_TLS;

        static constexpr size_t OWNERS = SERVERS + STREAM_CLIENTS + EVENT_CLIENTS;
    };

    static_assert(MemoryLimits::SERVERS > 0, "servers must be 1 to 16");
    static_assert(MemoryLimits::SERVERS <= OWNERS_PER_TYPE_LIMIT, "servers must be 1 to 16");
    static_assert(MemoryLimits::STREAM_CLIENTS > 0, "stream clients must be 1 to 16");
    static_assert(MemoryLimits::STREAM_CLIENTS <= OWNERS_PER_TYPE_LIMIT,
                  "stream clients must be 1 to 16");
    static_assert(MemoryLimits::EVENT_CLIENTS > 0, "event clients must be 1 to 16");
    static_assert(MemoryLimits::EVENT_CLIENTS <= OWNERS_PER_TYPE_LIMIT,
                  "event clients must be 1 to 16");
    static_assert(MemoryLimits::CONNECTIONS_PER_SERVER > 0,
                  "a server needs at least one connection");
    static_assert(MemoryLimits::CONNECTIONS_PER_SERVER <= CONNECTIONS_PER_SERVER_LIMIT,
                  "a server holds at most 1024 connections");
    static_assert(MemoryLimits::RECEIVE_CAPACITY > 0, "the receive stage needs capacity");
    static_assert(MemoryLimits::SEND_CAPACITY > 0, "the send stage needs capacity");
    static_assert(MemoryLimits::PUSH_CAPACITY > 0, "the push stage needs capacity");
}
