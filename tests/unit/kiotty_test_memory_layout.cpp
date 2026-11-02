#include "base/pool/kiotty_memory_layout.h"

#include <gtest/gtest.h>

using kiotty::PoolClass;
using kiotty::alignBlock;
using kiotty::net::ContextPoolLayout;
using kiotty::net::MemoryLimits;
using kiotty::net::TlsPoolLayout;

TEST(MemoryLimits, FollowTheBuildConfiguration)
{
    size_t owners = MemoryLimits::OWNERS;
    size_t per_server = MemoryLimits::CONNECTIONS_PER_SERVER;
    size_t connections = MemoryLimits::CONNECTIONS;
    bool tls = MemoryLimits::TLS;
    EXPECT_EQ(owners, static_cast<size_t>(KIOTTY_NET_MAX_OWNERS));
    EXPECT_EQ(per_server, static_cast<size_t>(KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER));
    EXPECT_EQ(connections, owners * per_server);
    EXPECT_EQ(tls, KIOTTY_NET_USE_TLS != 0);
}

TEST(ContextPoolLayout, ReservesEveryStageAsIfEveryOwnerWereAFullServer)
{
    const PoolClass* classes = ContextPoolLayout::classes();
    size_t count = ContextPoolLayout::CLASS_COUNT;
    size_t connections = MemoryLimits::CONNECTIONS;
    size_t tls_connections = MemoryLimits::TLS ? connections : 0;
    ASSERT_EQ(count, 5u);
    EXPECT_EQ(classes[0].block_bytes, alignBlock(MemoryLimits::RECV_CAPACITY));
    EXPECT_EQ(classes[0].block_count, connections);
    EXPECT_EQ(classes[1].block_bytes, alignBlock(MemoryLimits::SEND_CAPACITY));
    EXPECT_EQ(classes[1].block_count, connections);
    EXPECT_EQ(classes[2].block_bytes, alignBlock(MemoryLimits::PUSH_CAPACITY));
    EXPECT_EQ(classes[2].block_count, connections);
    EXPECT_EQ(classes[3].block_bytes, alignBlock(MemoryLimits::RECV_CAPACITY));
    EXPECT_EQ(classes[3].block_count, tls_connections);
    size_t send = MemoryLimits::SEND_CAPACITY;
    size_t push = MemoryLimits::PUSH_CAPACITY;
    EXPECT_GE(classes[4].block_bytes, send);
    EXPECT_GE(classes[4].block_bytes, push);
    EXPECT_EQ(classes[4].block_count, tls_connections);
}

TEST(ContextPoolLayout, CipherSendBlockHoldsTheLargestPlainMessageWithRecordOverhead)
{
    size_t plain = 16384;
    size_t cipher = kiotty::net::cipherSendBytes(plain);
    EXPECT_EQ(cipher, plain + 2 * 64 + 512);
    EXPECT_EQ(kiotty::net::cipherSendBytes(100), 100u + 64 + 512);
}

TEST(TlsPoolLayout, GrowsWithOwnersConnectionsAndTlsContexts)
{
    size_t contexts = MemoryLimits::TLS_CONTEXTS;
    size_t owners = MemoryLimits::OWNERS;
    size_t connections = MemoryLimits::CONNECTIONS;
    EXPECT_EQ(kiotty::net::tlsBlocks(7, 0, 0), 7u);
    EXPECT_EQ(kiotty::net::tlsBlocks(0, 1, 0), contexts * owners);
    EXPECT_EQ(kiotty::net::tlsBlocks(0, 0, 1), contexts * connections);
    const PoolClass* classes = TlsPoolLayout::classes();
    for (size_t i = 0; i < TlsPoolLayout::CLASS_COUNT; ++i)
    {
        EXPECT_EQ(classes[i].block_bytes % kiotty::POOL_ALIGNMENT, 0u) << i;
        EXPECT_GT(classes[i].block_count, 0u) << i;
    }
}
