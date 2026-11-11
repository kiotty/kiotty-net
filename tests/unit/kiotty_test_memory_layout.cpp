#include "data/datasource/kiotty_memory_layout.h"
#include "data/infra/tls/kiotty_tls_engine.h"

#include <gtest/gtest.h>

using kiotty::net::implementation::MemoryLimits;

TEST(MemoryLimits, FollowTheBuildConfiguration)
{
    bool tls = MemoryLimits::TLS;
    EXPECT_EQ(tls, KIOTTY_NET_USE_TLS != 0);
}

TEST(TlsLayout, CipherSendBytesHoldsSendThenNotifyWithRecordOverhead)
{
    size_t plain = 16384;
    EXPECT_EQ(kiotty::net::implementation::cipherSendBytes(plain), plain + 2 * 64 + 512);
    EXPECT_EQ(kiotty::net::implementation::cipherSendBytes(100), 100u + 64 + 512);
    size_t largest = kiotty::net::implementation::gatheredPlainBytes(MemoryLimits::SEND_CAPACITY, MemoryLimits::PUSH_CAPACITY);
    EXPECT_EQ(largest, MemoryLimits::SEND_CAPACITY + MemoryLimits::PUSH_CAPACITY + 64);
    size_t expected = MemoryLimits::TLS ? kiotty::net::implementation::alignStage(kiotty::net::implementation::cipherSendBytes(largest)) : 0;
    kiotty::net::implementation::Stage receive;
    kiotty::net::implementation::Stage send;
    kiotty::net::implementation::reserveTlsStages(kiotty::net::implementation::TlsStages{ receive, send }, kiotty::net::implementation::TlsCapacity{ MemoryLimits::RECEIVE_CAPACITY, largest });
    EXPECT_EQ(send.maximum(), expected);
    EXPECT_EQ(receive.maximum(), MemoryLimits::TLS ? MemoryLimits::RECEIVE_CAPACITY : 0);
}
