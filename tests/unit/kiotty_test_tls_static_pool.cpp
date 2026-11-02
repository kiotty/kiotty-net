#include "base/context/kiotty_context.h"
#include "base/tls/kiotty_tls_memory.h"
#include "support/kiotty_test_tls.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using kiotty::StaticPoolUsage;
using kiotty::net::Context;
using kiotty::net::TlsProfile;
using kiotty::net::tlsPoolClassCount;
using kiotty::net::tlsPoolUsage;
using kiotty_test::TlsPump;
using kiotty_test::clientConfig;
using kiotty_test::serverConfig;

namespace
{
    struct Live
    {
        size_t blocks;
        size_t failures;
    };

    Live live()
    {
        Live total = { 0, 0 };
        for (size_t i = 0; i < tlsPoolClassCount(); ++i)
        {
            StaticPoolUsage usage = tlsPoolUsage(i);
            total.blocks += usage.live;
            total.failures += usage.failures;
        }
        return total;
    }

    void connect(TlsPump& pump, TlsProfile& server, TlsProfile& client)
    {
        ASSERT_EQ(pump.open(server, client), KIOTTY_SUCCESS);
        pump.handshake();
        ASSERT_TRUE(pump.done());
    }
}

TEST(TlsStaticPool, ContextRoutesOpenSslIntoTheStaticPoolBeforeAnyTlsObjectExists)
{
    Context first;
    EXPECT_TRUE(first.tlsPooled());
    Context second;
    EXPECT_TRUE(second.tlsPooled());
    EXPECT_GT(tlsPoolClassCount(), 0u);
}

TEST(TlsStaticPool, HandshakeTakesBlocksFromTheStaticPoolAndClosingGivesThemBack)
{
    Context context;
    ASSERT_TRUE(context.tlsPooled());
    TlsProfile server;
    TlsProfile client;
    ASSERT_EQ(server.openServer(serverConfig()), KIOTTY_SUCCESS);
    ASSERT_EQ(client.openClient(clientConfig()), KIOTTY_SUCCESS);
    {
        TlsPump warm(context.pool(), 4096);
        connect(warm, server, client);
    }
    Live before = live();
    EXPECT_GT(before.blocks, 0u);
    {
        std::vector<std::unique_ptr<TlsPump>> pumps;
        for (int i = 0; i < 4; ++i)
        {
            pumps.emplace_back(new TlsPump(context.pool(), 4096));
            connect(*pumps.back(), server, client);
        }
        EXPECT_GT(live().blocks, before.blocks);
    }
    Live after = live();
    EXPECT_EQ(after.blocks, before.blocks);
    EXPECT_EQ(after.failures, 0u);
}
