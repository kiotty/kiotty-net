#include "base/data/infra/socket/kiotty_passive_socket.h"
#include "base/data/infra/socket/kiotty_socket_transfer.h"
#include "base/data/infra/tls/kiotty_tls_engine.h"
#include "base/data/infra/tls/kiotty_tls_pipe.h"
#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "base/data/repository/stream/kiotty_connection_stream.h"

#include "support/kiotty_test_os.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_text.h"
#include "support/kiotty_test_tls.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using kiotty::net::implementation::Span;
using kiotty::net::implementation::Stage;
using kiotty::net::implementation::StageBase;
using kiotty::net::implementation::ActiveSocket;
using kiotty::net::implementation::ActiveSocketResult;
using kiotty::net::implementation::ConnectionStream;
using kiotty::net::implementation::DecodeResult;
using kiotty::net::implementation::PassiveSocket;
using kiotty::net::implementation::SocketCode;
using kiotty::net::implementation::TlsEngine;
using kiotty::net::implementation::TlsProfile;
using kiotty_test::LoopbackPeer;

namespace
{
    const uint32_t WAIT_MS = 5000;
    const size_t   CAPACITY = 4096;
    const size_t   TLS_CHUNK = 1024;
    const size_t   TLS_RECORD = 16384;
    const char     TLS_APPLICATION_DATA = 0x17;
    const size_t   TLS_HEADER_BYTES = 5;

    KiottyNetAddress loopback(uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        address.transport = KIOTTY_NET_TRANSPORT_TCP;
        kiotty_test::copyHost(address.host, sizeof(address.host), "127.0.0.1");
        address.port = port;
        return address;
    }

    std::string pattern(size_t length)
    {
        std::string text(length, '\0');
        for (size_t i = 0; i < length; ++i)
        {
            text[i] = static_cast<char>('a' + i % 26);
        }
        return text;
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan span = { text.data(), text.size() };
        return span;
    }

    struct Side
    {
        explicit Side(size_t capacity = CAPACITY)
            : socket()
            , engine()
            , receive(capacity)
            , send(capacity)
            , tls_capacity{ capacity, capacity }
        {
            kiotty::net::implementation::reserveTlsStages(engine.stages(), tls_capacity);
            receive.limit(capacity);
            send.limit(capacity);
        }

        ConnectionStream stream()
        {
            return ConnectionStream(socket, engine, receive, send);
        }

        ActiveSocket                         socket;
        TlsEngine                            engine;
        Stage  receive{ kiotty::net::implementation::MemoryLimits::RECEIVE_CAPACITY };
        Stage  send{ kiotty::net::implementation::MemoryLimits::SEND_CAPACITY };
        kiotty::net::implementation::TlsCapacity                       tls_capacity;
    };

    bool connect(LoopbackPeer& peer, Side& side)
    {
        if (!peer.listenTcp(0))
        {
            return false;
        }
        ActiveSocketResult opened = ActiveSocket::open(KIOTTY_NET_TRANSPORT_TCP);
        if (!opened)
        {
            return false;
        }
        side.socket = std::move(opened.value);
        if (side.socket.connect(loopback(peer.port()), WAIT_MS) != SocketCode::SUCCESS)
        {
            return false;
        }
        return peer.accept(WAIT_MS);
    }

    bool pair(Side& server, Side& client)
    {
        PassiveSocket listener;
        if (listener.open(loopback(0), 1) != SocketCode::SUCCESS)
        {
            return false;
        }
        ActiveSocketResult opened = ActiveSocket::open(KIOTTY_NET_TRANSPORT_TCP);
        if (!opened)
        {
            return false;
        }
        client.socket = std::move(opened.value);
        if (client.socket.connect(loopback(listener.port()), WAIT_MS) != SocketCode::SUCCESS)
        {
            return false;
        }
        bool accepted = kiotty_test::waitUntil([&]() {
            ActiveSocketResult taken = listener.accept();
            if (!taken)
            {
                return false;
            }
            server.socket = std::move(taken.value);
            return true;
        }, WAIT_MS);
        return accepted;
    }

    struct Profiles
    {
        TlsProfile server;
        TlsProfile client;
    };

    bool handshake(Profiles& profiles, Side& server, Side& client)
    {
        if (profiles.server.openServer(kiotty_test::serverConfig()) != KIOTTY_NET_CODE_SUCCESS)
        {
            return false;
        }
        if (profiles.client.openClient(kiotty_test::clientConfig()) != KIOTTY_NET_CODE_SUCCESS)
        {
            return false;
        }
        if (!pair(server, client))
        {
            return false;
        }
        if (server.engine.open(profiles.server, kiotty_test::textOf(nullptr), server.tls_capacity) != KIOTTY_NET_CODE_SUCCESS)
        {
            return false;
        }
        if (client.engine.open(profiles.client, kiotty_test::textOf("localhost"), client.tls_capacity) != KIOTTY_NET_CODE_SUCCESS)
        {
            return false;
        }
        KiottyNetCode accepted = KIOTTY_NET_CODE_SUCCESS;
        std::thread peer([&]() {
            accepted = server.stream().handshake(WAIT_MS);
        });
        KiottyNetCode connected = client.stream().handshake(WAIT_MS);
        peer.join();
        EXPECT_EQ(connected, KIOTTY_NET_CODE_SUCCESS);
        EXPECT_EQ(accepted, KIOTTY_NET_CODE_SUCCESS);
        return connected == KIOTTY_NET_CODE_SUCCESS && accepted == KIOTTY_NET_CODE_SUCCESS;
    }

    std::string readExactly(ConnectionStream stream, size_t length)
    {
        std::string received;
        char buffer[CAPACITY];
        while (received.size() < length)
        {
            KiottyNetResult result = stream.read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
            if (result.code != KIOTTY_NET_CODE_SUCCESS)
            {
                break;
            }
            received.append(buffer, static_cast<size_t>(result.bytes));
        }
        return received;
    }

    std::string writeFileWith(const std::string& content)
    {
        std::string path = kiotty_test::uniqueSocketPath() + ".bin";
        std::ofstream file(path.c_str(), std::ios::binary);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path;
    }

    std::string readRaw(ActiveSocket& socket, uint32_t quiet_milliseconds)
    {
        std::string bytes;
        char buffer[CAPACITY];
        while (waitReadable(socket, quiet_milliseconds) == SocketCode::SUCCESS)
        {
            kiotty::net::implementation::TransferResult received = receive(socket, Span<char>{ buffer, sizeof(buffer) });
            if (!received)
            {
                break;
            }
            bytes.append(buffer, received.value);
        }
        return bytes;
    }

    std::string decodeAll(TlsEngine& engine, const std::string& cipher)
    {
        std::string plain;
        char buffer[CAPACITY];
        size_t at = 0;
        for (;;)
        {
            StageBase& incoming = engine.stages().receive;
            size_t room = incoming.provider().writable();
            size_t piece = cipher.size() - at < room ? cipher.size() - at : room;
            kiotty_test::put(incoming, cipher.substr(at, piece));
            at += piece;
            DecodeResult decoded = engine.decode(Span<char>{ buffer, sizeof(buffer) });
            if (!decoded)
            {
                break;
            }
            plain.append(buffer, decoded.value);
            if (decoded.value == 0 && at == cipher.size())
            {
                break;
            }
        }
        return plain;
    }

    size_t applicationRecords(const std::string& bytes)
    {
        size_t count = 0;
        size_t at = 0;
        while (at + TLS_HEADER_BYTES <= bytes.size())
        {
            size_t length = static_cast<unsigned char>(bytes[at + 3]) * 256u + static_cast<unsigned char>(bytes[at + 4]);
            if (bytes[at] == TLS_APPLICATION_DATA)
            {
                ++count;
            }
            at += TLS_HEADER_BYTES + length;
        }
        return count;
    }
}

#define REQUIRE_TLS()                                            \
    do                                                           \
    {                                                            \
        if (!kiotty::net::implementation::tlsSupported())                        \
        {                                                        \
            GTEST_SKIP() << "built with KIOTTY_NET_USE_TLS=OFF"; \
        }                                                        \
    } while (false)

TEST(ConnectionStream, ReadReturnsStagedPlaintextBeforeTouchingSocket)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    kiotty_test::put(side.receive, "abc");
    ASSERT_TRUE(peer.sendAll("xyz", 3));
    ASSERT_EQ(waitReadable(side.socket, WAIT_MS), SocketCode::SUCCESS);
    char buffer[16];
    KiottyNetResult staged = side.stream().read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
    ASSERT_EQ(staged.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(std::string(buffer, static_cast<size_t>(staged.bytes)), "abc");
    KiottyNetResult fresh = side.stream().read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
    ASSERT_EQ(fresh.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(std::string(buffer, static_cast<size_t>(fresh.bytes)), "xyz");
}

TEST(ConnectionStream, ReadBlocksUntilPeerSendsThenReturnsUpToLength)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::thread sender([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        peer.sendAll("hello world", 11);
    });
    char buffer[4];
    KiottyNetResult result = side.stream().read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
    uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
    sender.join();
    ASSERT_EQ(result.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(std::string(buffer, static_cast<size_t>(result.bytes)), "hell");
    EXPECT_GE(elapsed, 80u);
}

TEST(ConnectionStream, ReadTimesOut)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    char buffer[4];
    KiottyNetResult result = side.stream().read(Span<char>{ buffer, sizeof(buffer) }, 100);
    EXPECT_EQ(result.code, KIOTTY_NET_CODE_TIMEOUT);
    EXPECT_EQ(result.bytes, 0);
    EXPECT_GE(kiotty_test::elapsedMilliseconds(started), 90u);
}

TEST(ConnectionStream, ReadReturnsPeerClosedWhenThePeerShutsDown)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.shutdownWrite());
    char buffer[4];
    KiottyNetResult result = side.stream().read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
    EXPECT_EQ(result.code, KIOTTY_NET_CODE_PEER_CLOSED);
    EXPECT_EQ(result.bytes, 0);
}

TEST(ConnectionStream, ReadAndWriteRejectEmptyOrMissingBuffers)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    char buffer[4];
    EXPECT_EQ(side.stream().read(Span<char>{ nullptr, sizeof(buffer) }, WAIT_MS).code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
    EXPECT_EQ(side.stream().read(Span<char>{ buffer, 0 }, WAIT_MS).code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
    KiottyNetSpan missing = { nullptr, 4 };
    EXPECT_EQ(side.stream().write(missing, WAIT_MS).code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
}

TEST(ConnectionStream, WritePlaintextSendsAllWithoutCopy)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(kiotty_test::setSendBuffer(side.socket.handle(), 4096));
    std::string data = pattern(1024 * 1024);
    std::string received(data.size(), '\0');
    bool arrived = false;
    std::thread receiver([&]() {
        arrived = peer.recvExactly(&received[0], received.size(), WAIT_MS);
    });
    KiottyNetResult result = side.stream().write(spanOf(data), WAIT_MS);
    receiver.join();
    EXPECT_EQ(result.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(static_cast<size_t>(result.bytes), data.size());
    EXPECT_TRUE(arrived);
    EXPECT_EQ(received, data);
    EXPECT_EQ(side.send.consumer().readable(), 0u);
}

TEST(ConnectionStream, WriteFileSendsExactRange)
{
    LoopbackPeer peer;
    Side side(1024);
    ASSERT_TRUE(connect(peer, side));
    std::string content = pattern(10000);
    std::string path = writeFileWith(content);
    std::string received(5000, '\0');
    bool arrived = false;
    std::thread receiver([&]() {
        arrived = peer.recvExactly(&received[0], received.size(), WAIT_MS);
    });
    KiottyNetCode code = side.stream().writeFile(kiotty_test::textOf(path.c_str()), 1000, 5000, WAIT_MS);
    receiver.join();
    kiotty_test::removeFile(path.c_str());
    EXPECT_EQ(code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_TRUE(arrived);
    EXPECT_EQ(received, content.substr(1000, 5000));
    char extra = 0;
    EXPECT_FALSE(peer.recvExactly(&extra, 1, 100));
    EXPECT_EQ(side.send.consumer().readable(), 0u);
}

TEST(ConnectionStream, WriteFileMissingPathIsInvalidArgument)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    std::string path = writeFileWith(pattern(100));
    std::string missing = path + ".missing";
    EXPECT_EQ(side.stream().writeFile(kiotty_test::textOf(missing.c_str()), 0, 1, WAIT_MS), KIOTTY_NET_CODE_INVALID_ARGUMENT);
    EXPECT_EQ(side.stream().writeFile(kiotty_test::textOf(nullptr), 0, 1, WAIT_MS), KIOTTY_NET_CODE_INVALID_ARGUMENT);
    EXPECT_EQ(side.stream().writeFile(kiotty_test::textOf(path.c_str()), 50, 51, WAIT_MS), KIOTTY_NET_CODE_INVALID_ARGUMENT);
    EXPECT_EQ(side.stream().writeFile(kiotty_test::textOf(path.c_str()), 101, 0, WAIT_MS), KIOTTY_NET_CODE_INVALID_ARGUMENT);
    kiotty_test::removeFile(path.c_str());
    char extra = 0;
    EXPECT_FALSE(peer.recvExactly(&extra, 1, 100));
}

TEST(ConnectionStream, ConnectedIsFalseAfterPeerClose)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    EXPECT_TRUE(side.stream().connected());
    peer.close();
    EXPECT_TRUE(kiotty_test::waitUntil([&]() { return !side.stream().connected(); }, WAIT_MS));
}

TEST(ConnectionStream, HandshakeCompletesAgainstBlockingPeer)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side());
    ASSERT_TRUE(handshake(profiles, *server, *client));
    std::string pong;
    std::thread peer([&]() {
        std::string ping = readExactly(server->stream(), 4);
        if (ping == "ping")
        {
            server->stream().write(spanOf(std::string("pong")), WAIT_MS);
        }
    });
    KiottyNetResult sent = client->stream().write(spanOf(std::string("ping")), WAIT_MS);
    pong = readExactly(client->stream(), 4);
    peer.join();
    EXPECT_EQ(sent.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(sent.bytes, 4);
    EXPECT_EQ(pong, "pong");
}

TEST(ConnectionStream, HandshakeWithoutAnOpenEngineIsInvalidArgument)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    EXPECT_EQ(side.stream().handshake(WAIT_MS), KIOTTY_NET_CODE_INVALID_ARGUMENT);
}

TEST(ConnectionStream, WriteTlsSendsFullRecordsStraightFromTheEngine)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side(kiotty::net::implementation::MemoryLimits::RECEIVE_CAPACITY));
    std::unique_ptr<Side> client(new Side(TLS_CHUNK));
    ASSERT_TRUE(handshake(profiles, *server, *client));
    std::string data = pattern(40000);
    std::string cipher;
    std::thread receiver([&]() {
        cipher = readRaw(server->socket, 300);
    });
    KiottyNetResult sent = client->stream().write(spanOf(data), WAIT_MS);
    receiver.join();
    ASSERT_EQ(sent.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(static_cast<size_t>(sent.bytes), data.size());
    EXPECT_EQ(applicationRecords(cipher), (data.size() + TLS_RECORD - 1) / TLS_RECORD);
    EXPECT_EQ(client->engine.stages().send.consumer().readable(), 0u);
    EXPECT_EQ(client->send.consumer().readable(), 0u);
    EXPECT_EQ(decodeAll(server->engine, cipher), data);
}

TEST(ConnectionStream, ReadTlsDecodesStraightIntoTheCallersBuffer)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side());
    ASSERT_TRUE(handshake(profiles, *server, *client));
    std::string data = pattern(20000);
    std::thread sender([&]() {
        server->stream().write(spanOf(data), WAIT_MS);
    });
    std::string received;
    std::string buffer(data.size(), '\0');
    bool staged = false;
    while (received.size() < data.size())
    {
        KiottyNetResult result = client->stream().read(Span<char>{ &buffer[0], buffer.size() }, WAIT_MS);
        if (result.code != KIOTTY_NET_CODE_SUCCESS)
        {
            break;
        }
        received.append(buffer.data(), static_cast<size_t>(result.bytes));
        staged = staged || client->receive.consumer().readable() > 0;
    }
    sender.join();
    EXPECT_EQ(received, data);
    EXPECT_FALSE(staged);
    EXPECT_EQ(client->engine.stages().receive.consumer().readable(), 0u);
}

TEST(ConnectionStream, WriteFileOverTlsSendsExactRange)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side(TLS_CHUNK));
    ASSERT_TRUE(handshake(profiles, *server, *client));
    std::string content = pattern(10000);
    std::string path = writeFileWith(content);
    std::string received;
    std::thread receiver([&]() {
        received = readExactly(server->stream(), 5000);
    });
    KiottyNetCode code = client->stream().writeFile(kiotty_test::textOf(path.c_str()), 1000, 5000, WAIT_MS);
    receiver.join();
    kiotty_test::removeFile(path.c_str());
    EXPECT_EQ(code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(received, content.substr(1000, 5000));
    EXPECT_EQ(client->send.consumer().readable(), 0u);
}

TEST(ConnectionStream, FlushSendsLeftoverCipherSendFirst)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side());
    ASSERT_TRUE(handshake(profiles, *server, *client));
    client->engine.encode(spanOf(std::string("first")));
    ASSERT_GT(client->engine.stages().send.consumer().readable(), 0u);
    KiottyNetResult sent = client->stream().write(spanOf(std::string("second")), WAIT_MS);
    EXPECT_EQ(sent.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(client->engine.stages().send.consumer().readable(), 0u);
    EXPECT_EQ(readExactly(server->stream(), 11), "firstsecond");
}

namespace
{
    std::string lineOf(const kiotty::net::implementation::LineResult& line)
    {
        return std::string(line.value.data, line.value.length);
    }

    std::string readOnce(ConnectionStream stream)
    {
        char buffer[CAPACITY];
        KiottyNetResult result = stream.read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS);
        if (result.code != KIOTTY_NET_CODE_SUCCESS)
        {
            return std::string();
        }
        return std::string(buffer, static_cast<size_t>(result.bytes));
    }
}

TEST(ConnectionStream, ReadLineStripsTheLineEndAndLeavesTheRestForRead)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    const std::string sent = "HTTP/1.1 200 OK\r\nA: b\n\r\nbody";
    ASSERT_TRUE(peer.sendAll(sent.data(), sent.size()));
    kiotty::net::implementation::LineResult first = readLine(side.stream(), WAIT_MS);
    ASSERT_EQ(first.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(lineOf(first), "HTTP/1.1 200 OK");
    kiotty::net::implementation::LineResult second = readLine(side.stream(), WAIT_MS);
    ASSERT_EQ(second.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(lineOf(second), "A: b");
    kiotty::net::implementation::LineResult empty = readLine(side.stream(), WAIT_MS);
    ASSERT_EQ(empty.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(empty.value.length, 0u);
    std::string rest = readOnce(side.stream());
    while (rest.size() < 4)
    {
        const std::string more = readOnce(side.stream());
        if (more.empty())
        {
            break;
        }
        rest += more;
    }
    EXPECT_EQ(rest, "body");
}

TEST(ConnectionStream, ReadLineWaitsForTheLineFeedAcrossReceives)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.sendAll("ab", 2));
    std::thread sender([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        peer.sendAll("c", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        peer.sendAll("d\r\nnext", 7);
    });
    kiotty::net::implementation::LineResult line = readLine(side.stream(), WAIT_MS);
    sender.join();
    ASSERT_EQ(line.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(lineOf(line), "abcd");
    EXPECT_EQ(readExactly(side.stream(), 4), "next");
}

TEST(ConnectionStream, ReadLineLongerThanTheStageIsBufferFull)
{
    LoopbackPeer peer;
    Side side(16);
    ASSERT_TRUE(connect(peer, side));
    const std::string sent(32, 'x');
    ASSERT_TRUE(peer.sendAll(sent.data(), sent.size()));
    kiotty::net::implementation::LineResult line = readLine(side.stream(), WAIT_MS);
    EXPECT_EQ(line.code, KIOTTY_NET_CODE_BUFFER_FULL);
}

TEST(ConnectionStream, ReadLineThatFillsTheStageExactlyIsReturned)
{
    LoopbackPeer peer;
    Side side(16);
    ASSERT_TRUE(connect(peer, side));
    const std::string sent = std::string(14, 'y') + "\r\n";
    ASSERT_TRUE(peer.sendAll(sent.data(), sent.size()));
    kiotty::net::implementation::LineResult line = readLine(side.stream(), WAIT_MS);
    ASSERT_EQ(line.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(lineOf(line), std::string(14, 'y'));
}

TEST(ConnectionStream, ReadLineTimesOutAndKeepsThePartialLine)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.sendAll("part", 4));
    kiotty::net::implementation::LineResult line = readLine(side.stream(), 100);
    EXPECT_EQ(line.code, KIOTTY_NET_CODE_TIMEOUT);
    ASSERT_TRUE(peer.sendAll("ial\n", 4));
    kiotty::net::implementation::LineResult whole = readLine(side.stream(), WAIT_MS);
    ASSERT_EQ(whole.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(lineOf(whole), "partial");
}

TEST(ConnectionStream, ReadLineReturnsPeerClosedWhenThePeerLeavesMidLine)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.sendAll("half", 4));
    ASSERT_TRUE(peer.shutdownWrite());
    kiotty::net::implementation::LineResult line = readLine(side.stream(), WAIT_MS);
    EXPECT_EQ(line.code, KIOTTY_NET_CODE_PEER_CLOSED);
}

TEST(ConnectionStream, PeekReceivesUntilTheWantedBytesAreStaged)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    kiotty_test::put(side.receive, "ab");
    kiotty::net::implementation::StagedResult already = side.stream().peek(2, WAIT_MS);
    ASSERT_EQ(already.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(std::string(already.value.data, already.value.length), "ab");
    ASSERT_TRUE(peer.sendAll("cd", 2));
    kiotty::net::implementation::StagedResult staged = side.stream().peek(4, WAIT_MS);
    ASSERT_EQ(staged.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(std::string(staged.value.data, staged.value.length), "abcd");
    side.stream().consume(2);
    kiotty::net::implementation::StagedResult rest = side.stream().peek(0, WAIT_MS);
    ASSERT_EQ(rest.code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(std::string(rest.value.data, rest.value.length), "cd");
}

TEST(ConnectionStream, ReadLineTlsDecodesIntoTheStage)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side());
    ASSERT_TRUE(handshake(profiles, *server, *client));
    std::thread sender([&]() {
        server->stream().write(spanOf(std::string("line one\r\nline two\r\ntail")), WAIT_MS);
    });
    kiotty::net::implementation::LineResult first = readLine(client->stream(), WAIT_MS);
    const std::string first_text = first ? lineOf(first) : std::string();
    kiotty::net::implementation::LineResult second = readLine(client->stream(), WAIT_MS);
    const std::string second_text = second ? lineOf(second) : std::string();
    std::string tail = readExactly(client->stream(), 4);
    sender.join();
    EXPECT_EQ(first_text, "line one");
    EXPECT_EQ(second_text, "line two");
    EXPECT_EQ(tail, "tail");
}

namespace
{
    const KiottyNetCode ARRIVED = KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING;
    const size_t        FLOOD_BYTES = 16 * 1024 * 1024;
    const size_t        PIECE_BYTES = 1024 * 1024;
    const size_t        FLOOD_PIECES = 512;
    const uint32_t      ARRIVAL_DELAY_MS = 200;
    const uint32_t      SHORT_WAIT_MS = 300;

    bool waitArrival(ActiveSocket& socket)
    {
        return waitReadable(socket, WAIT_MS) == SocketCode::SUCCESS;
    }

    KiottyNetResult writeOnward(ConnectionStream stream, const std::string& data, size_t from)
    {
        size_t done = from;
        for (;;)
        {
            const KiottyNetSpan rest = { data.data() + done, data.size() - done };
            const KiottyNetResult sent = stream.write(rest, WAIT_MS);
            done += static_cast<size_t>(sent.bytes);
            if (sent.code != ARRIVED) { return KiottyNetResult{ static_cast<int32_t>(done), sent.code }; }
        }
    }

    struct Flooded
    {
        KiottyNetResult last;
        size_t          whole;
    };

    Flooded floodUntilStopped(ConnectionStream stream, const std::string& piece, uint32_t timeout_milliseconds)
    {
        for (size_t whole = 0; whole < FLOOD_PIECES; ++whole)
        {
            const KiottyNetResult sent = stream.write(spanOf(piece), timeout_milliseconds);
            if (sent.code != KIOTTY_NET_CODE_SUCCESS) { return Flooded{ sent, whole }; }
        }
        return Flooded{ KiottyNetResult{ 0, KIOTTY_NET_CODE_SUCCESS }, FLOOD_PIECES };
    }

    std::string receiveExactly(LoopbackPeer& peer, size_t length)
    {
        std::string received(length, 'x');
        if (!peer.recvExactly(&received[0], length, WAIT_MS)) { return std::string(); }
        return received;
    }
}

TEST(ConnectionStream, WriteStopsAtOnceWhenBytesWaitBeforeIt)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.sendAll("413", 3));
    ASSERT_TRUE(waitArrival(side.socket));
    const KiottyNetResult sent = side.stream().write(spanOf(std::string("body")), WAIT_MS);
    EXPECT_EQ(ARRIVED, sent.code);
    EXPECT_EQ(0, sent.bytes);
    EXPECT_EQ("413", readOnce(side.stream()));
    const KiottyNetResult again = side.stream().write(spanOf(std::string("body")), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ(4, again.bytes);
    EXPECT_EQ("body", receiveExactly(peer, 4));
}

TEST(ConnectionStream, BlockedWriteStopsWhenThePeerAnswersAndGoesOnFromWhereItStopped)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    const std::string piece = pattern(PIECE_BYTES);
    std::thread answer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_DELAY_MS));
        peer.sendAll("stop", 4);
    });
    const Flooded stopped = floodUntilStopped(side.stream(), piece, WAIT_MS);
    answer.join();
    ASSERT_EQ(ARRIVED, stopped.last.code);
    EXPECT_LT(static_cast<size_t>(stopped.last.bytes), piece.size());
    EXPECT_EQ("stop", readOnce(side.stream()));
    size_t mismatched = 0;
    std::thread drain([&]() {
        std::string received(piece.size(), 'x');
        for (size_t i = 0; i <= stopped.whole; ++i)
        {
            const bool is_same = peer.recvExactly(&received[0], received.size(), WAIT_MS) && received == piece;
            mismatched += is_same ? 0 : 1;
        }
    });
    const KiottyNetResult rest = writeOnward(side.stream(), piece, static_cast<size_t>(stopped.last.bytes));
    drain.join();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, rest.code);
    EXPECT_EQ(static_cast<int32_t>(piece.size()), rest.bytes);
    EXPECT_EQ(0u, mismatched);
}

TEST(ConnectionStream, WriteAfterThePeerEndedIsPeerClosedAndTheBytesBeforeStayReadable)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.sendAll("bye", 3));
    ASSERT_TRUE(peer.shutdownWrite());
    ASSERT_TRUE(waitArrival(side.socket));
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_DELAY_MS));
    EXPECT_EQ(ARRIVED, side.stream().write(spanOf(std::string("x")), WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, side.stream().write(spanOf(std::string("x")), WAIT_MS).code);
    EXPECT_EQ("bye", readOnce(side.stream()));
    char buffer[8];
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, side.stream().read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS).code);
}

TEST(ConnectionStream, BytesTakenBeforeAResetStayReadableOnEveryPlatform)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    ASSERT_TRUE(peer.sendAll("HTTP/1.1 413", 12));
    ASSERT_TRUE(waitArrival(side.socket));
    ASSERT_EQ(ARRIVED, side.stream().write(spanOf(std::string("body")), WAIT_MS).code);
    ASSERT_TRUE(peer.reset());
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_DELAY_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_SOCKET_ERROR, side.stream().write(spanOf(std::string("body")), WAIT_MS).code);
    EXPECT_EQ("HTTP/1.1 413", readOnce(side.stream()));
    char buffer[8];
    const KiottyNetCode after = side.stream().read(Span<char>{ buffer, sizeof(buffer) }, WAIT_MS).code;
    EXPECT_TRUE(after == KIOTTY_NET_CODE_SOCKET_ERROR || after == KIOTTY_NET_CODE_PEER_CLOSED) << after;
}

TEST(ConnectionStream, FullStageStopsWatchingSoTheWriteGoesOnAndTimesOutWithoutSpinning)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    const std::string flood = pattern(2 * CAPACITY);
    ASSERT_TRUE(peer.sendAll(flood.data(), flood.size()));
    ASSERT_TRUE(kiotty_test::waitUntil([&]() {
        side.stream().write(spanOf(std::string("x")), WAIT_MS);
        return side.receive.consumer().readable() == CAPACITY;
    }, WAIT_MS));
    const KiottyNetResult sent = side.stream().write(spanOf(std::string("y")), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, sent.code);
    const std::string piece = pattern(PIECE_BYTES);
    const Flooded filled = floodUntilStopped(side.stream(), piece, SHORT_WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_TIMEOUT, filled.last.code);
    const std::clock_t cpu = std::clock();
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const KiottyNetResult blocked = side.stream().write(spanOf(piece), SHORT_WAIT_MS);
    const uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, blocked.code);
    EXPECT_GE(elapsed + 20, SHORT_WAIT_MS);
#if !defined(_WIN32)
    const double busy = static_cast<double>(std::clock() - cpu) * 1000.0 / CLOCKS_PER_SEC;
    EXPECT_LT(busy, SHORT_WAIT_MS / 2.0);
#else
    static_cast<void>(cpu);
#endif
    EXPECT_EQ(flood.substr(0, 16), readOnce(side.stream()).substr(0, 16));
}

TEST(ConnectionStream, TlsWriteStopsOnlyForApplicationBytes)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side());
    ASSERT_TRUE(handshake(profiles, *server, *client));
    kiotty::net::implementation::TlsPipe(server->engine).requestKeyUpdate();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server->stream().write(spanOf(std::string()), WAIT_MS).code);
    ASSERT_TRUE(waitArrival(client->socket));
    const KiottyNetResult quiet = client->stream().write(spanOf(std::string("ping")), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, quiet.code);
    EXPECT_EQ(4, quiet.bytes);
    EXPECT_EQ("ping", readExactly(server->stream(), 4));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, server->stream().write(spanOf(std::string("pong")), WAIT_MS).code);
    ASSERT_TRUE(waitArrival(client->socket));
    const KiottyNetResult stopped = client->stream().write(spanOf(std::string("more")), WAIT_MS);
    EXPECT_EQ(ARRIVED, stopped.code);
    EXPECT_EQ(0, stopped.bytes);
    EXPECT_EQ("pong", readExactly(client->stream(), 4));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, client->stream().write(spanOf(std::string("more")), WAIT_MS).code);
    EXPECT_EQ("more", readExactly(server->stream(), 4));
}

TEST(ConnectionStream, BlockedTlsWriteStopsWhenThePeerAnswersAndGoesOnWithoutLosingARecord)
{
    REQUIRE_TLS();
    Profiles profiles;
    std::unique_ptr<Side> server(new Side());
    std::unique_ptr<Side> client(new Side());
    ASSERT_TRUE(handshake(profiles, *server, *client));
    const std::string data = pattern(FLOOD_BYTES);
    std::thread answer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_DELAY_MS));
        writeOnward(server->stream(), std::string("stop"), 0);
    });
    const KiottyNetResult stopped = client->stream().write(spanOf(data), WAIT_MS);
    answer.join();
    ASSERT_EQ(ARRIVED, stopped.code);
    EXPECT_GT(stopped.bytes, 0);
    EXPECT_LT(static_cast<size_t>(stopped.bytes), data.size());
    EXPECT_EQ("stop", readExactly(client->stream(), 4));
    std::string received;
    std::thread drain([&]() { received = readExactly(server->stream(), data.size()); });
    const KiottyNetResult rest = writeOnward(client->stream(), data, static_cast<size_t>(stopped.bytes));
    drain.join();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, rest.code);
    EXPECT_TRUE(received == data);
}

TEST(ConnectionStream, WriteFileLooksForArrivalOnlyAtTheStart)
{
    LoopbackPeer peer;
    Side side;
    ASSERT_TRUE(connect(peer, side));
    const std::string content = pattern(1000);
    const std::string path = writeFileWith(content);
    ASSERT_TRUE(peer.sendAll("no", 2));
    ASSERT_TRUE(waitArrival(side.socket));
    EXPECT_EQ(ARRIVED, side.stream().writeFile(kiotty_test::textOf(path.c_str()), 0, 1000, WAIT_MS));
    EXPECT_EQ("no", readOnce(side.stream()));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, side.stream().writeFile(kiotty_test::textOf(path.c_str()), 0, 1000, WAIT_MS));
    EXPECT_EQ(content, receiveExactly(peer, 1000));
    kiotty_test::removeFile(path.c_str());
}
