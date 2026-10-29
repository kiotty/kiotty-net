#include "base/socket/kiotty_socket.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <utility>

using kiotty::net::ActiveSocket;
using kiotty::net::ActiveSocketResult;
using kiotty::net::PassiveSocket;
using kiotty::net::SocketCode;
using kiotty_test::LoopbackPeer;

namespace
{
    KiottyNetEndpoint endpointOf(KiottyNetTransport transport, const std::string& host, uint16_t port)
    {
        KiottyNetEndpoint endpoint;
        std::memset(&endpoint, 0, sizeof(endpoint));
        endpoint.transport = transport;
        kiotty_test::copyHost(endpoint.host, sizeof(endpoint.host), host);
        endpoint.port = port;
        return endpoint;
    }
}

TEST(PassiveSocket, OpenOnPortZeroBindsAnEphemeralPort)
{
    PassiveSocket listener;
    KiottyNetEndpoint endpoint = endpointOf(KIOTTY_TCP, "127.0.0.1", 0);
    ASSERT_EQ(listener.open(endpoint, 8), SocketCode::SUCCESS);
    EXPECT_TRUE(listener.valid());
    EXPECT_GT(listener.port(), 0);
}

TEST(PassiveSocket, OpenOnBusyPortReturnsAddressInUse)
{
    PassiveSocket first;
    ASSERT_EQ(first.open(endpointOf(KIOTTY_TCP, "127.0.0.1", 0), 8), SocketCode::SUCCESS);
    PassiveSocket second;
    EXPECT_EQ(second.open(endpointOf(KIOTTY_TCP, "127.0.0.1", first.port()), 8), SocketCode::ADDRESS_IN_USE);
    EXPECT_FALSE(second.valid());
}

TEST(PassiveSocket, OpenUnixDomainUnlinksStalePathFirst)
{
    std::string path = kiotty_test::uniqueSocketPath();
    ASSERT_TRUE(kiotty_test::createFile(path.c_str()));
    PassiveSocket listener;
    EXPECT_EQ(listener.open(endpointOf(KIOTTY_PIPE, path, 0), 8), SocketCode::SUCCESS);
    LoopbackPeer peer;
    EXPECT_TRUE(peer.connectUnix(path));
    listener.close();
    kiotty_test::removeFile(path.c_str());
}

TEST(PassiveSocket, OpenWithPathOverLimitReturnsInvalidArgument)
{
    std::string path(200, 'p');
    PassiveSocket listener;
    EXPECT_EQ(listener.open(endpointOf(KIOTTY_PIPE, path, 0), 8), SocketCode::INVALID_ARGUMENT);
    EXPECT_FALSE(listener.valid());
}

TEST(PassiveSocket, AcceptAfterPeerConnectReturnsConnectedSocket)
{
    PassiveSocket listener;
    ASSERT_EQ(listener.open(endpointOf(KIOTTY_TCP, "127.0.0.1", 0), 8), SocketCode::SUCCESS);
    LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(listener.port()));
    ActiveSocket accepted;
    bool ready = kiotty_test::waitUntil([&]() {
        ActiveSocketResult result = listener.accept();
        if (!result.isOk())
        {
            return false;
        }
        accepted = std::move(result.value());
        return true;
    }, 2000);
    ASSERT_TRUE(ready);
    EXPECT_STREQ(accepted.peer().hostname, "127.0.0.1");
    ASSERT_TRUE(accepted.send("hi", 2).isOk());
    char buffer[2];
    ASSERT_TRUE(peer.recvExactly(buffer, sizeof(buffer), 2000));
    EXPECT_EQ(std::memcmp(buffer, "hi", 2), 0);
}

TEST(PassiveSocket, AcceptWithNoPendingConnectionReturnsWouldBlock)
{
    PassiveSocket listener;
    ASSERT_EQ(listener.open(endpointOf(KIOTTY_TCP, "127.0.0.1", 0), 8), SocketCode::SUCCESS);
    ActiveSocketResult result = listener.accept();
    EXPECT_EQ(result.code(), SocketCode::WOULD_BLOCK);
}
