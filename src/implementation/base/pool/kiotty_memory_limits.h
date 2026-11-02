#if !defined(KIOTTY_MEMORY_LIMITS_H)
#define KIOTTY_MEMORY_LIMITS_H

#include "core/kiotty_pool_handle.h"

#include <cstddef>

#if !defined(KIOTTY_NET_MAX_OWNERS)
#define KIOTTY_NET_MAX_OWNERS 16
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

#if !defined(KIOTTY_NET_MAX_TLS_CONTEXTS)
#define KIOTTY_NET_MAX_TLS_CONTEXTS 1
#endif

#if !defined(KIOTTY_NET_USE_TLS)
#define KIOTTY_NET_USE_TLS 0
#endif

namespace kiotty
{
    namespace net
    {
        static const size_t OWNER_SLOTS = static_cast<size_t>(1) << OWNER_SLOT_BITS;
        static const size_t CONTEXT_SLOTS = static_cast<size_t>(1) << CONTEXT_SLOT_BITS;
        static const size_t CONNECTIONS_PER_SERVER_LIMIT = 1024;

        struct MemoryLimits
        {
            static const size_t OWNERS = KIOTTY_NET_MAX_OWNERS;
            static const size_t CONNECTIONS_PER_SERVER = KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER;
            static const size_t RECV_CAPACITY = KIOTTY_NET_MAX_RECV_CAPACITY;
            static const size_t SEND_CAPACITY = KIOTTY_NET_MAX_SEND_CAPACITY;
            static const size_t PUSH_CAPACITY = KIOTTY_NET_MAX_PUSH_CAPACITY;
            static const size_t TLS_CONTEXTS = KIOTTY_NET_MAX_TLS_CONTEXTS;
            static const bool   TLS = KIOTTY_NET_USE_TLS != 0;

            static const size_t CONNECTIONS = OWNERS * CONNECTIONS_PER_SERVER;
        };

        static_assert(MemoryLimits::OWNERS > 0, "a context needs at least one owner slot");
        static_assert(MemoryLimits::OWNERS <= OWNER_SLOTS, "owners must fit the owner bits of a handle");
        static_assert(MemoryLimits::CONNECTIONS_PER_SERVER > 0, "a server needs at least one connection");
        static_assert(MemoryLimits::CONNECTIONS_PER_SERVER <= CONNECTIONS_PER_SERVER_LIMIT, "a server holds at most 1024 connections");
        static_assert(MemoryLimits::RECV_CAPACITY > 0, "the receive stage needs capacity");
        static_assert(MemoryLimits::SEND_CAPACITY > 0, "the send stage needs capacity");
        static_assert(MemoryLimits::PUSH_CAPACITY > 0, "the push stage needs capacity");
        static_assert(MemoryLimits::TLS_CONTEXTS > 0, "the tls pool needs at least one context");
        static_assert(MemoryLimits::TLS_CONTEXTS <= CONTEXT_SLOTS, "tls contexts must fit the context bits of a handle");
    }
}

#endif
