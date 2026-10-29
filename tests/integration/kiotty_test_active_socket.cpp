#include "base/socket/kiotty_socket.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>
#include <utility>

using kiotty::net::ActiveSocket;
using kiotty::net::ActiveSocketResult;
using kiotty::net::SocketCode;
using kiotty::net::TransferResult;
using kiotty_test::LoopbackPeer;

namespace
{
    KiottyNetEndpoint tcpEndpoint(const std::string& host, uint16_t port)
    {
        KiottyNetEndpoint endpoint;
        std::memset(&endpoint, 0, sizeof(endpoint));
        endpoint.transport = KIOTTY_TCP;
        kiotty_test::copyHost(endpoint.host, sizeof(endpoint.host), host);
        endpoint.port = port;
        return endpoint;
    }

    ActiveSocket openTcp()
    {
        ActiveSocketResult opened = ActiveSocket::open(KIOTTY_TCP);
        EXPECT_TRUE(opened.isOk());
        return std::move(opened.value());
    }

    ActiveSocket connectedTo(LoopbackPeer& peer)
    {
        EXPECT_TRUE(peer.listenTcp(0));
        ActiveSocket socket = openTcp();
        KiottyNetEndpoint endpoint = tcpEndpoint("127.0.0.1", peer.port());
        EXPECT_EQ(socket.connect(endpoint, 2000), SocketCode::SUCCESS);
        EXPECT_TRUE(peer.accept(2000));
        return socket;
    }

    SocketCode receiveUntilNotWouldBlock(ActiveSocket& socket, char* buffer, size_t length, size_t& received)
    {
        SocketCode code = SocketCode::WOULD_BLOCK;
        kiotty_test::waitUntil([&]() {
            TransferResult result = socket.receive(buffer, length);
            code = result.code();
            if (result.isOk())
            {
                received = result.value();
            }
            return code != SocketCode::WOULD_BLOCK;
        }, 2000);
        return code;
    }
}

TEST(ActiveSocket, OpenTcpGivesValidNonBlockingSocket)
{
    LoopbackPeer peer;
    ActiveSocket socket = connectedTo(peer);
    EXPECT_TRUE(socket.valid());
    EXPECT_NE(socket.handle(), kiotty::net::INVALID_SOCKET_HANDLE);
    char buffer[8];
    TransferResult result = socket.receive(buffer, sizeof(buffer));
    EXPECT_EQ(result.code(), SocketCode::WOULD_BLOCK);
}

TEST(ActiveSocket, ConnectToListeningLoopbackSucceeds)
{
    LoopbackPeer peer;
    ASSERT_TRUE(peer.listenTcp(0));
    ActiveSocket socket = openTcp();
    KiottyNetEndpoint endpoint = tcpEndpoint("127.0.0.1", peer.port());
    EXPECT_EQ(socket.connect(endpoint, 2000), SocketCode::SUCCESS);
    KiottyNetConnectionInfo info = socket.peer();
    EXPECT_STREQ(info.hostname, "127.0.0.1");
    EXPECT_EQ(info.port, static_cast<int32_t>(peer.port()));
}

TEST(ActiveSocket, ConnectToClosedPortReturnsRefusedBeforeTimeout)
{
    uint16_t port = kiotty_test::unusedPort();
    ActiveSocket socket = openTcp();
    KiottyNetEndpoint endpoint = tcpEndpoint("127.0.0.1", port);
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    EXPECT_EQ(socket.connect(endpoint, 5000), SocketCode::REFUSED);
    EXPECT_LT(kiotty_test::elapsedMs(started), 5000u);
}

TEST(ActiveSocket, ConnectTimeoutExpiresOnUnroutableAddress)
{
    ActiveSocket socket = openTcp();
    KiottyNetEndpoint endpoint = tcpEndpoint("10.255.255.1", 9);
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    SocketCode code = socket.connect(endpoint, 200);
    uint32_t elapsed = kiotty_test::elapsedMs(started);
    if (code != SocketCode::TIMEOUT)
    {
        GTEST_SKIP() << "no unroutable path on this host, code " << static_cast<int>(code);
    }
    EXPECT_GE(elapsed, 190u);
    EXPECT_LT(elapsed, 1500u);
}

TEST(ActiveSocket, SetNoDelayOnTcpIsReflectedByGetsockopt)
{
    ActiveSocket socket = openTcp();
    EXPECT_EQ(socket.setNoDelay(true), SocketCode::SUCCESS);
    EXPECT_TRUE(kiotty_test::noDelayOf(socket.handle()));
    EXPECT_EQ(socket.setNoDelay(false), SocketCode::SUCCESS);
    EXPECT_FALSE(kiotty_test::noDelayOf(socket.handle()));
}

TEST(ActiveSocket, SetNoDelayOnUnixDomainSocketReturnsSuccessAndDoesNothing)
{
    ActiveSocketResult opened = ActiveSocket::open(KIOTTY_PIPE);
    ASSERT_TRUE(opened.isOk());
    EXPECT_EQ(opened.value().setNoDelay(true), SocketCode::SUCCESS);
    EXPECT_EQ(opened.value().setNoDelay(false), SocketCode::SUCCESS);
}

TEST(ActiveSocket, SendAndReceiveRoundTripOnLoopback)
{
    LoopbackPeer peer;
    ActiveSocket socket = connectedTo(peer);
    TransferResult sent = socket.send("ping", 4);
    ASSERT_TRUE(sent.isOk());
    EXPECT_EQ(sent.value(), 4u);
    char echoed[4];
    ASSERT_TRUE(peer.recvExactly(echoed, sizeof(echoed), 2000));
    EXPECT_EQ(std::memcmp(echoed, "ping", 4), 0);
    ASSERT_TRUE(peer.sendAll("pong", 4));
    char buffer[16];
    size_t received = 0;
    EXPECT_EQ(receiveUntilNotWouldBlock(socket, buffer, sizeof(buffer), received), SocketCode::SUCCESS);
    EXPECT_EQ(received, 4u);
    EXPECT_EQ(std::memcmp(buffer, "pong", 4), 0);
}

TEST(ActiveSocket, ReceiveAfterPeerShutdownReturnsClosed)
{
    LoopbackPeer peer;
    ActiveSocket socket = connectedTo(peer);
    ASSERT_TRUE(peer.shutdownWrite());
    char buffer[16];
    size_t received = 0;
    EXPECT_EQ(receiveUntilNotWouldBlock(socket, buffer, sizeof(buffer), received), SocketCode::CLOSED);
}

TEST(ActiveSocket, ReceiveWithNothingPendingReturnsWouldBlock)
{
    LoopbackPeer peer;
    ActiveSocket socket = connectedTo(peer);
    char buffer[16];
    TransferResult result = socket.receive(buffer, sizeof(buffer));
    EXPECT_EQ(result.code(), SocketCode::WOULD_BLOCK);
}

TEST(ActiveSocket, CloseTwiceIsSafeAndMovedFromSocketIsInvalid)
{
    ActiveSocket socket = openTcp();
    socket.close();
    EXPECT_FALSE(socket.valid());
    EXPECT_EQ(socket.handle(), kiotty::net::INVALID_SOCKET_HANDLE);
    ActiveSocket other = openTcp();
    socket.close();
    EXPECT_TRUE(other.valid());
    EXPECT_EQ(other.setNoDelay(true), SocketCode::SUCCESS);

    ActiveSocket moved(std::move(other));
    EXPECT_FALSE(other.valid());
    EXPECT_TRUE(moved.valid());
    ActiveSocket assigned;
    assigned = std::move(moved);
    EXPECT_FALSE(moved.valid());
    EXPECT_TRUE(assigned.valid());
}
