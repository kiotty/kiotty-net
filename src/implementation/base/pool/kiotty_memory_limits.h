#if !defined(KIOTTY_MEMORY_LIMITS_H)
#define KIOTTY_MEMORY_LIMITS_H

#include <cstddef>

#if !defined(KIOTTY_NET_MAX_SERVERS)
#define KIOTTY_NET_MAX_SERVERS 4
#endif

#if !defined(KIOTTY_NET_MAX_STREAM_CLIENTS)
#define KIOTTY_NET_MAX_STREAM_CLIENTS 8
#endif

#if !defined(KIOTTY_NET_MAX_EVENT_CLIENTS)
#define KIOTTY_NET_MAX_EVENT_CLIENTS 4
#endif

#if !defined(KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER)
#define KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER 128
#endif

#if !defined(KIOTTY_NET_MAX_RECV_CAPACITY)
#define KIOTTY_NET_MAX_RECV_CAPACITY 16384
#endif

#if !defined(KIOTTY_NET_MAX_SEND_CAPACITY)
#define KIOTTY_NET_MAX_SEND_CAPACITY 16384
#endif

#if !defined(KIOTTY_NET_MAX_PUSH_CAPACITY)
#define KIOTTY_NET_MAX_PUSH_CAPACITY 16384
#endif

#if !defined(KIOTTY_NET_USE_TLS)
#define KIOTTY_NET_USE_TLS 0
#endif

namespace kiotty
{
    namespace net
    {
        static const size_t OWNERS_PER_TYPE_LIMIT = 16;
        static const size_t CONNECTIONS_PER_SERVER_LIMIT = 1024;

        struct MemoryLimits
        {
            static const size_t SERVERS = KIOTTY_NET_MAX_SERVERS;
            static const size_t STREAM_CLIENTS = KIOTTY_NET_MAX_STREAM_CLIENTS;
            static const size_t EVENT_CLIENTS = KIOTTY_NET_MAX_EVENT_CLIENTS;
            static const size_t CONNECTIONS_PER_SERVER = KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER;
            static const size_t RECV_CAPACITY = KIOTTY_NET_MAX_RECV_CAPACITY;
            static const size_t SEND_CAPACITY = KIOTTY_NET_MAX_SEND_CAPACITY;
            static const size_t PUSH_CAPACITY = KIOTTY_NET_MAX_PUSH_CAPACITY;
            static const bool   TLS = KIOTTY_NET_USE_TLS != 0;

            static const size_t OWNERS = SERVERS + STREAM_CLIENTS + EVENT_CLIENTS;
        };

        static_assert(MemoryLimits::SERVERS > 0 && MemoryLimits::SERVERS <= OWNERS_PER_TYPE_LIMIT, "servers must be 1 to 16");
        static_assert(MemoryLimits::STREAM_CLIENTS > 0 && MemoryLimits::STREAM_CLIENTS <= OWNERS_PER_TYPE_LIMIT, "stream clients must be 1 to 16");
        static_assert(MemoryLimits::EVENT_CLIENTS > 0 && MemoryLimits::EVENT_CLIENTS <= OWNERS_PER_TYPE_LIMIT, "event clients must be 1 to 16");
        static_assert(MemoryLimits::CONNECTIONS_PER_SERVER > 0, "a server needs at least one connection");
        static_assert(MemoryLimits::CONNECTIONS_PER_SERVER <= CONNECTIONS_PER_SERVER_LIMIT, "a server holds at most 1024 connections");
        static_assert(MemoryLimits::RECV_CAPACITY > 0, "the receive stage needs capacity");
        static_assert(MemoryLimits::SEND_CAPACITY > 0, "the send stage needs capacity");
        static_assert(MemoryLimits::PUSH_CAPACITY > 0, "the push stage needs capacity");
    }
}

#endif
