#if !defined(KIOTTY_MEMORY_LAYOUT_H)
#define KIOTTY_MEMORY_LAYOUT_H

#include "base/pool/kiotty_memory_limits.h"
#include "core/pool/kiotty_pool_layout.h"

namespace kiotty
{
    namespace net
    {
        typedef MemoryLimits Limits;

        static const size_t TLS_RECORD_PLAIN_BYTES = 16384;
        static const size_t TLS_RECORD_OVERHEAD_BYTES = 64;
        static const size_t TLS_CONTROL_RESERVE_BYTES = 512;

        constexpr size_t largerOf(size_t left, size_t right)
        {
            return left > right ? left : right;
        }

        constexpr size_t cipherSendBytes(size_t plain)
        {
            return plain + (plain / TLS_RECORD_PLAIN_BYTES + 1) * TLS_RECORD_OVERHEAD_BYTES + TLS_CONTROL_RESERVE_BYTES;
        }

        constexpr size_t whenTls(size_t count)
        {
            return Limits::TLS ? count : 0;
        }

        static const size_t CIPHER_RECEIVE_BYTES = whenTls(Limits::RECV_CAPACITY);
        static const size_t CIPHER_SEND_BYTES = whenTls(alignBlock(cipherSendBytes(largerOf(Limits::SEND_CAPACITY, Limits::PUSH_CAPACITY))));

        struct GlobalTlsBlocks
        {
            static constexpr size_t count(size_t global, size_t thread, size_t, size_t)
            {
                return whenTls(global + thread * Limits::OWNERS);
            }
        };

        struct ServerTlsBlocks
        {
            static constexpr size_t count(size_t, size_t, size_t profile, size_t connection)
            {
                return whenTls(profile + connection * Limits::CONNECTIONS_PER_SERVER);
            }
        };

        struct ClientTlsBlocks
        {
            static constexpr size_t count(size_t, size_t, size_t profile, size_t connection)
            {
                return whenTls(profile + connection);
            }
        };

        template <class Blocks>
        using TlsLayoutOf = PoolLayout<
            BlockClass<32, Blocks::count(5633, 22, 387, 159)>,
            BlockClass<48, Blocks::count(1753, 3, 89, 43)>,
            BlockClass<64, Blocks::count(101, 33, 15, 16)>,
            BlockClass<96, Blocks::count(540, 16, 19, 12)>,
            BlockClass<128, Blocks::count(2719, 47, 14, 16)>,
            BlockClass<192, Blocks::count(91, 8, 10, 14)>,
            BlockClass<256, Blocks::count(99, 0, 11, 9)>,
            BlockClass<384, Blocks::count(293, 2, 46, 6)>,
            BlockClass<512, Blocks::count(25, 6, 7, 8)>,
            BlockClass<768, Blocks::count(7, 0, 5, 4)>,
            BlockClass<1024, Blocks::count(9, 18, 3, 6)>,
            BlockClass<1536, Blocks::count(5, 0, 4, 2)>,
            BlockClass<2048, Blocks::count(5, 0, 2, 2)>,
            BlockClass<3072, Blocks::count(6, 0, 4, 1)>,
            BlockClass<4096, Blocks::count(9, 0, 2, 5)>,
            BlockClass<6144, Blocks::count(9, 0, 1, 5)>,
            BlockClass<8192, Blocks::count(2, 0, 0, 0)>,
            BlockClass<12288, Blocks::count(0, 0, 0, 1)>,
            BlockClass<24576, Blocks::count(3, 0, 0, 2)>,
            BlockClass<49152, Blocks::count(1, 0, 0, 0)>>;

        typedef TlsLayoutOf<GlobalTlsBlocks> GlobalTlsLayout;
        typedef TlsLayoutOf<ServerTlsBlocks> ServerTlsLayout;
        typedef TlsLayoutOf<ClientTlsBlocks> ClientTlsLayout;
    }
}

#endif
