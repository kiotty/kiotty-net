#include "context/kiotty_context.h"

#include "context/kiotty_configuration.h"
#include "context/kiotty_tls_configuration.h"
#include "data/datasource/kiotty_memory_limits.h"
#include "support/kiotty_test_peer.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#if KIOTTY_NET_HAS_EVENT_BACKEND

namespace net = kiotty::net::implementation;

namespace
{
    const uint16_t CLOSED_PORT = 9;

    KiottyNetServerConfiguration serverConfiguration()
    {
        KiottyNetServerConfiguration configuration{};
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTcpServerConfiguration(configuration, "127.0.0.1", kiotty_test::unusedPort()));
        return configuration;
    }

    KiottyNetClientConfiguration clientConfiguration()
    {
        KiottyNetClientConfiguration configuration{};
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, net::initializeTcpClientConfiguration(configuration, "127.0.0.1", CLOSED_PORT));
        return configuration;
    }

    class ContextTest : public ::testing::Test
    {
    protected:
        ContextTest()
            : context(nullptr)
        {
        }

        void SetUp() override
        {
            net::ContextResult opened = net::Context::open();
            ASSERT_TRUE(opened);
            context = &opened.value();
        }

        void TearDown() override
        {
            if (context != nullptr)
            {
                context->release();
            }
        }

        net::Context* context;
    };

    void exitWithoutRelease()
    {
        net::ContextResult opened = net::Context::open();
        if (!opened)
        {
            std::exit(1);
        }
        KiottyNetServerConfiguration server = serverConfiguration();
        KiottyNetClientConfiguration client = clientConfiguration();
        net::ServerResult created_server = opened.value().createServer(server, nullptr);
        net::EventClientResult created_event = opened.value().createEventClient(client, nullptr);
        net::StreamClientResult created_stream = opened.value().createStreamClient(client, nullptr);
        if (!created_server || !created_event || !created_stream)
        {
            std::exit(2);
        }
        std::exit(0);
    }
}

TEST_F(ContextTest, InitializeContextTwiceReturnsTheSameContextAndSuccess)
{
    net::ContextResult again = net::Context::open();
    ASSERT_TRUE(again);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ(context, &again.value());
}

TEST_F(ContextTest, ReleaseOnceEndsBoth)
{
    net::ContextResult again = net::Context::open();
    ASSERT_TRUE(again);
    context->release();
    KiottyNetServerConfiguration server = serverConfiguration();
    net::ServerResult refused = again.value().createServer(server, nullptr);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, refused.code);
    net::ContextResult reopened = net::Context::open();
    ASSERT_TRUE(reopened);
    EXPECT_EQ(context, &reopened.value());
    net::ServerResult created = reopened.value().createServer(server, nullptr);
    ASSERT_TRUE(created);
    reopened.value().destroy(created.value());
}

TEST_F(ContextTest, RetiredOwnerIsReusedByTheNextCreateOfTheSameType)
{
    KiottyNetServerConfiguration server = serverConfiguration();
    KiottyNetClientConfiguration client = clientConfiguration();

    net::ServerResult first_server = context->createServer(server, nullptr);
    ASSERT_TRUE(first_server);
    net::Server* first_server_address = &first_server.value();
    context->destroy(first_server.value());
    net::ServerResult second_server = context->createServer(server, nullptr);
    ASSERT_TRUE(second_server);
    EXPECT_EQ(first_server_address, &second_server.value());

    net::EventClientResult first_event = context->createEventClient(client, nullptr);
    ASSERT_TRUE(first_event);
    net::EventClient* first_event_address = &first_event.value();
    context->destroy(first_event.value());
    net::EventClientResult second_event = context->createEventClient(client, nullptr);
    ASSERT_TRUE(second_event);
    EXPECT_EQ(first_event_address, &second_event.value());

    net::StreamClientResult first_stream = context->createStreamClient(client, nullptr);
    ASSERT_TRUE(first_stream);
    net::StreamClient* first_stream_address = &first_stream.value();
    context->destroy(first_stream.value());
    net::StreamClientResult second_stream = context->createStreamClient(client, nullptr);
    ASSERT_TRUE(second_stream);
    EXPECT_EQ(first_stream_address, &second_stream.value());
}

TEST_F(ContextTest, CreateServerIsTableFullAfterMaxServers)
{
    KiottyNetServerConfiguration server = serverConfiguration();
    std::vector<net::Server*> servers;
    for (size_t i = 0; i < net::MemoryLimits::SERVERS; ++i)
    {
        net::ServerResult created = context->createServer(server, nullptr);
        ASSERT_TRUE(created);
        servers.push_back(&created.value());
    }
    net::ServerResult refused = context->createServer(server, nullptr);
    EXPECT_EQ(KIOTTY_NET_CODE_TABLE_FULL, refused.code);
    context->destroy(*servers.back());
    net::ServerResult reused = context->createServer(server, nullptr);
    ASSERT_TRUE(reused);
    EXPECT_EQ(servers.back(), &reused.value());
}

TEST_F(ContextTest, CreateRejectsConfigurationsThatWereNotInitialized)
{
    KiottyNetServerConfiguration server{};
    KiottyNetClientConfiguration client{};
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, context->createServer(server, nullptr).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, context->createEventClient(client, nullptr).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, context->createStreamClient(client, nullptr).code);

    KiottyNetServerConfiguration valid_server = serverConfiguration();
    KiottyNetClientConfiguration valid_client = clientConfiguration();
    KiottyNetTlsServerConfiguration server_tls{};
    KiottyNetTlsClientConfiguration client_tls{};
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, context->createServer(valid_server, &server_tls).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, context->createEventClient(valid_client, &client_tls).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, context->createStreamClient(valid_client, &client_tls).code);
}

TEST(ContextDeathTest, ProcessExitWithoutReleaseContextDoesNotCrash)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(exitWithoutRelease(), ::testing::ExitedWithCode(0), "");
}

#endif
