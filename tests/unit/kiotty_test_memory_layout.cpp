#include "base/pool/kiotty_memory_layout.h"

#include <gtest/gtest.h>

using kiotty::net::MemoryLimits;

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

TEST(TlsLayout, CipherSendBytesHoldsTheLargestPlainMessageWithRecordOverhead)
{
    size_t plain = 16384;
    EXPECT_EQ(kiotty::net::cipherSendBytes(plain), plain + 2 * 64 + 512);
    EXPECT_EQ(kiotty::net::cipherSendBytes(100), 100u + 64 + 512);
    size_t largest = kiotty::net::largerOf(MemoryLimits::SEND_CAPACITY, MemoryLimits::PUSH_CAPACITY);
    size_t expected = MemoryLimits::TLS ? kiotty::net::alignStage(kiotty::net::cipherSendBytes(largest)) : 0;
    EXPECT_EQ(kiotty::net::CIPHER_SEND_BYTES, expected);
}
