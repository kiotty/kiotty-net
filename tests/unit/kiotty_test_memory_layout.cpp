#include "base/pool/kiotty_memory_layout.h"

#include <gtest/gtest.h>

using kiotty::PoolClass;
using kiotty::net::ClientTlsLayout;
using kiotty::net::GlobalTlsLayout;
using kiotty::net::MemoryLimits;
using kiotty::net::ServerTlsLayout;

namespace
{
    template <class Layout>
    void expectAlignedClasses()
    {
        const PoolClass* classes = Layout::classes();
        for (size_t i = 0; i < Layout::CLASS_COUNT; ++i)
        {
            EXPECT_EQ(classes[i].block_bytes % kiotty::POOL_ALIGNMENT, 0u) << i;
        }
    }

    template <class Layout>
    size_t blocksOf(size_t index)
    {
        return Layout::classes()[index].block_count;
    }
}

TEST(MemoryLimits, FollowTheBuildConfiguration)
{
    size_t servers = MemoryLimits::SERVERS;
    size_t stream_clients = MemoryLimits::STREAM_CLIENTS;
    size_t event_clients = MemoryLimits::EVENT_CLIENTS;
    size_t per_server = MemoryLimits::CONNECTIONS_PER_SERVER;
    bool tls = MemoryLimits::TLS;
    EXPECT_EQ(servers, static_cast<size_t>(KIOTTY_NET_MAX_SERVERS));
    EXPECT_EQ(stream_clients, static_cast<size_t>(KIOTTY_NET_MAX_STREAM_CLIENTS));
    EXPECT_EQ(event_clients, static_cast<size_t>(KIOTTY_NET_MAX_EVENT_CLIENTS));
    EXPECT_EQ(per_server, static_cast<size_t>(KIOTTY_NET_MAX_CONNECTIONS_PER_SERVER));
    EXPECT_EQ(tls, KIOTTY_NET_USE_TLS != 0);
}

TEST(TlsLayout, EveryClassIsSixteenByteAlignedAndHasBlocksOnlyWithTls)
{
    expectAlignedClasses<GlobalTlsLayout>();
    expectAlignedClasses<ServerTlsLayout>();
    expectAlignedClasses<ClientTlsLayout>();
    if (!MemoryLimits::TLS)
    {
        EXPECT_EQ(GlobalTlsLayout::ARENA_BYTES, 0u);
        EXPECT_EQ(ServerTlsLayout::ARENA_BYTES, 0u);
        EXPECT_EQ(ClientTlsLayout::ARENA_BYTES, 0u);
        return;
    }
    EXPECT_GT(GlobalTlsLayout::ARENA_BYTES, 0u);
    EXPECT_GT(ServerTlsLayout::ARENA_BYTES, ClientTlsLayout::ARENA_BYTES);
    EXPECT_GT(ClientTlsLayout::ARENA_BYTES, 0u);
}

TEST(TlsLayout, ServerLayoutScalesWithConnectionsPerServerAndClientLayoutHoldsOne)
{
    if (!MemoryLimits::TLS)
    {
        GTEST_SKIP() << "built with KIOTTY_NET_USE_TLS=OFF";
    }
    size_t per_server = MemoryLimits::CONNECTIONS_PER_SERVER;
    EXPECT_EQ(blocksOf<GlobalTlsLayout>(0), 5096u);
    EXPECT_EQ(blocksOf<ServerTlsLayout>(0), 381u + 149u * per_server);
    EXPECT_EQ(blocksOf<ClientTlsLayout>(0), 381u + 149u);
    EXPECT_EQ(blocksOf<ServerTlsLayout>(16), 0u);
    EXPECT_EQ(blocksOf<GlobalTlsLayout>(17), 0u);
    EXPECT_EQ(blocksOf<ClientTlsLayout>(17), 1u);
}

TEST(TlsLayout, CipherSendBytesHoldsTheLargestPlainMessageWithRecordOverhead)
{
    size_t plain = 16384;
    EXPECT_EQ(kiotty::net::cipherSendBytes(plain), plain + 2 * 64 + 512);
    EXPECT_EQ(kiotty::net::cipherSendBytes(100), 100u + 64 + 512);
    size_t largest = kiotty::net::largerOf(MemoryLimits::SEND_CAPACITY, MemoryLimits::PUSH_CAPACITY);
    size_t expected = MemoryLimits::TLS ? kiotty::alignBlock(kiotty::net::cipherSendBytes(largest)) : 0;
    EXPECT_EQ(kiotty::net::CIPHER_SEND_BYTES, expected);
}
