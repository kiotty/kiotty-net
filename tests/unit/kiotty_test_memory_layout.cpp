#include "data/datasource/kiotty_memory_layout.h"

#include <gtest/gtest.h>

using kiotty::net::implementation::MemoryLimits;

TEST(MemoryLimits, FollowTheBuildConfiguration)
{
    bool tls = MemoryLimits::TLS;
    EXPECT_EQ(tls, KIOTTY_NET_USE_TLS != 0);
}

TEST(TlsLayout, CipherSendBytesHoldsTheLargestPlainMessageWithRecordOverhead)
{
    size_t plain = 16384;
    EXPECT_EQ(kiotty::net::implementation::cipherSendBytes(plain), plain + 2 * 64 + 512);
    EXPECT_EQ(kiotty::net::implementation::cipherSendBytes(100), 100u + 64 + 512);
    size_t largest = kiotty::net::implementation::largerOf(MemoryLimits::SEND_CAPACITY, MemoryLimits::PUSH_CAPACITY);
    size_t expected = MemoryLimits::TLS ? kiotty::net::implementation::alignStage(kiotty::net::implementation::cipherSendBytes(largest)) : 0;
    EXPECT_EQ(kiotty::net::implementation::CIPHER_SEND_BYTES, expected);
}
