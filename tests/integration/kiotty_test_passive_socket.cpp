#include "data/infra/socket/kiotty_passive_socket.h"
#include "data/infra/socket/kiotty_socket_transfer.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <utility>

using kiotty::net::implementation::Span;
using kiotty::net::implementation::ActiveSocket;
using kiotty::net::implementation::ActiveSocketResult;
using kiotty::net::implementation::PassiveSocket;
using kiotty::net::implementation::SocketCode;
using kiotty_test::LoopbackPeer;

namespace
{
    KiottyNetAddress addressOf(KiottyNetTransport transport, const std::string& host, uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        address.transport = transport;
        kiotty_test::copyHost(address.host, sizeof(address.host), host);
        address.port = port;
        return address;
    }
}

TEST(PassiveSocket, OpenOnPortZeroBindsAnEphemeralPort)
{
    PassiveSocket listener;
    KiottyNetAddress address = addressOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", 0);
    ASSERT_EQ(listener.open(address, 8), SocketCode::SUCCESS);
    EXPECT_TRUE(listener.valid());
    EXPECT_GT(listener.port(), 0);
}

TEST(PassiveSocket, OpenOnBusyPortReturnsAddressInUse)
{
    PassiveSocket first;
    ASSERT_EQ(first.open(addressOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", 0), 8), SocketCode::SUCCESS);
    PassiveSocket second;
    EXPECT_EQ(second.open(addressOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", first.port()), 8), SocketCode::ADDRESS_IN_USE);
    EXPECT_FALSE(second.valid());
}

TEST(PassiveSocket, OpenUnixDomainUnlinksStalePathFirst)
{
    std::string path = kiotty_test::uniqueSocketPath();
    ASSERT_TRUE(kiotty_test::createFile(path.c_str()));
    PassiveSocket listener;
    EXPECT_EQ(listener.open(addressOf(KIOTTY_NET_TRANSPORT_PIPE, path, 0), 8), SocketCode::SUCCESS);
    LoopbackPeer peer;
    EXPECT_TRUE(peer.connectUnix(path));
    listener.close();
    kiotty_test::removeFile(path.c_str());
}

TEST(PassiveSocket, OpenWithPathOverLimitReturnsInvalidArgument)
{
    std::string path(200, 'p');
    PassiveSocket listener;
    EXPECT_EQ(listener.open(addressOf(KIOTTY_NET_TRANSPORT_PIPE, path, 0), 8), SocketCode::INVALID_ARGUMENT);
    EXPECT_FALSE(listener.valid());
}

TEST(PassiveSocket, AcceptAfterPeerConnectReturnsConnectedSocket)
{
    PassiveSocket listener;
    ASSERT_EQ(listener.open(addressOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", 0), 8), SocketCode::SUCCESS);
    LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(listener.port()));
    ActiveSocket accepted;
    bool ready = kiotty_test::waitUntil([&]() {
        ActiveSocketResult result = listener.accept();
        if (!result)
        {
            return false;
        }
        accepted = std::move(result.value);
        return true;
    }, 2000);
    ASSERT_TRUE(ready);
    EXPECT_STREQ(accepted.peer().hostname, "127.0.0.1");
    ASSERT_TRUE(send(accepted, Span<const char>{ "hi", 2 }));
    char buffer[2];
    ASSERT_TRUE(peer.recvExactly(buffer, sizeof(buffer), 2000));
    EXPECT_EQ(std::memcmp(buffer, "hi", 2), 0);
}

TEST(PassiveSocket, AcceptWithNoPendingConnectionReturnsWouldBlock)
{
    PassiveSocket listener;
    ASSERT_EQ(listener.open(addressOf(KIOTTY_NET_TRANSPORT_TCP, "127.0.0.1", 0), 8), SocketCode::SUCCESS);
    ActiveSocketResult result = listener.accept();
    EXPECT_EQ(result.code, SocketCode::WOULD_BLOCK);
}
