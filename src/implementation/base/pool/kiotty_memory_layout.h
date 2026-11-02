#if !defined(KIOTTY_MEMORY_LAYOUT_H)
#define KIOTTY_MEMORY_LAYOUT_H

#include "base/pool/kiotty_memory_limits.h"
#include "core/kiotty_pool_layout.h"

namespace kiotty
{
    namespace net
    {
        typedef MemoryLimits Limits;

        constexpr size_t largerOf(size_t left, size_t right)
        {
            return left > right ? left : right;
        }

        constexpr size_t cipherSendBytes(size_t plain)
        {
            return plain + (plain / 16384 + 1) * 64 + 512;
        }

        constexpr size_t whenTls(size_t count)
        {
            return Limits::TLS ? count : 0;
        }

        typedef PoolLayout<
            BlockClass<alignBlock(Limits::RECV_CAPACITY), Limits::CONNECTIONS>,
            BlockClass<alignBlock(Limits::SEND_CAPACITY), Limits::CONNECTIONS>,
            BlockClass<alignBlock(Limits::PUSH_CAPACITY), Limits::CONNECTIONS>,
            BlockClass<alignBlock(Limits::RECV_CAPACITY), whenTls(Limits::CONNECTIONS)>,
            BlockClass<alignBlock(cipherSendBytes(largerOf(Limits::SEND_CAPACITY, Limits::PUSH_CAPACITY))), whenTls(Limits::CONNECTIONS)>
        > ContextPoolLayout;

        constexpr size_t tlsBlocks(size_t global, size_t profile, size_t connection)
        {
            return global + Limits::TLS_CONTEXTS * (profile * Limits::OWNERS + connection * Limits::CONNECTIONS);
        }

        typedef PoolLayout<
            BlockClass<32, tlsBlocks(5096, 381, 149)>,
            BlockClass<48, tlsBlocks(1354, 89, 43)>,
            BlockClass<64, tlsBlocks(67, 15, 16)>,
            BlockClass<96, tlsBlocks(56, 18, 12)>,
            BlockClass<128, tlsBlocks(1933, 14, 16)>,
            BlockClass<192, tlsBlocks(31, 10, 14)>,
            BlockClass<256, tlsBlocks(93, 11, 8)>,
            BlockClass<384, tlsBlocks(291, 46, 5)>,
            BlockClass<512, tlsBlocks(25, 7, 7)>,
            BlockClass<768, tlsBlocks(7, 5, 4)>,
            BlockClass<1024, tlsBlocks(7, 3, 6)>,
            BlockClass<1536, tlsBlocks(5, 4, 2)>,
            BlockClass<2048, tlsBlocks(3, 2, 2)>,
            BlockClass<3072, tlsBlocks(6, 4, 1)>,
            BlockClass<4096, tlsBlocks(3, 2, 5)>,
            BlockClass<6144, tlsBlocks(1, 1, 5)>,
            BlockClass<8192, tlsBlocks(1, 0, 0)>,
            BlockClass<12288, tlsBlocks(0, 0, 1)>,
            BlockClass<24576, tlsBlocks(0, 0, 2)>,
            BlockClass<49152, tlsBlocks(1, 0, 0)>
        > TlsPoolLayout;
    }
}

#endif
