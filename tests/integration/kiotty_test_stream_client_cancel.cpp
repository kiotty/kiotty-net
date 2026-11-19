#include "kiotty_net_base.h"

#include "base/data/infra/tls/kiotty_tls_profile.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace
{
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;
    const uint32_t CANCEL_DELAY_MS = 300;
    const uint32_t WAKE_BOUND_MS = 1500;
    const size_t   PIECE_BYTES = 1024u * 1024u;
    const size_t   PATTERN_PERIOD = 251;
    const size_t   FILL_LIMIT_BYTES = 1024u * 1024u * 1024u;
    const uint32_t FILL_WAIT_MS = 100;

    struct Timed
    {
        KiottyNetResult result;
        uint32_t        elapsed;
    };

    KiottyNetSpan spanOf(const char* data, size_t length)
    {
        KiottyNetSpan span = { data, length };
        return span;
    }

    KiottyNetAddress loopbackAddress(uint16_t port)
    {
        KiottyNetAddress address;
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port));
        return address;
    }

    std::string certPath(const char* name)
    {
        return std::string(KIOTTY_TEST_CERT_DIR) + "/" + name;
    }

    std::vector<char> patternBytes()
    {
        std::vector<char> bytes(PIECE_BYTES + PATTERN_PERIOD);
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<char>(i % PATTERN_PERIOD);
        }
        return bytes;
    }

    KiottyNetSpan pieceAt(const std::vector<char>& pattern, size_t offset)
    {
        return spanOf(pattern.data() + offset % PATTERN_PERIOD, PIECE_BYTES);
    }

    size_t fillUntilBlocked(KiottyNetStreamClientHandle client, const std::vector<char>& pattern)
    {
        size_t sent = 0;
        while (sent < FILL_LIMIT_BYTES)
        {
            const KiottyNetResult written = KiottyNet_writeStreamClient(client, pieceAt(pattern, sent), FILL_WAIT_MS);
            sent += static_cast<size_t>(written.bytes);
            if (written.code == KIOTTY_NET_CODE_TIMEOUT)
            {
                return sent;
            }
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, written.code);
            if (written.code != KIOTTY_NET_CODE_SUCCESS)
            {
                return sent;
            }
        }
        ADD_FAILURE() << "the peer never stopped taking bytes";
        return sent;
    }

    bool receivesThePattern(kiotty_test::LoopbackPeer& peer, size_t length)
    {
        std::vector<char> received(PIECE_BYTES);
        size_t done = 0;
        while (done < length)
        {
            const size_t piece = std::min(PIECE_BYTES, length - done);
            if (!peer.recvExactly(received.data(), piece, WAIT_MS))
            {
                return false;
            }
            for (size_t i = 0; i < piece; ++i)
            {
                if (received[i] != static_cast<char>((done + i) % PATTERN_PERIOD))
                {
                    return false;
                }
            }
            done += piece;
        }
        return true;
    }

    template <typename Call>
    Timed cancelDuring(KiottyNetStreamClientHandle client, Call call)
    {
        std::thread canceller([client]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(CANCEL_DELAY_MS));
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_cancelStreamClient(client));
        });
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        const KiottyNetResult result = call();
        const uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
        canceller.join();
        Timed timed = { result, elapsed };
        return timed;
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        return KiottyNet_acceptConnection(app_context);
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)context;
        (void)connection;
        return KiottyNet_thenHold(0);
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)context;
        (void)connection;
        (void)data;
        return KiottyNet_thenHold(0);
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length)
    {
        (void)context;
        (void)connection;
        (void)length;
        return KiottyNet_thenHold(0);
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)context;
        (void)connection;
        return KiottyNet_thenHold(0);
    }

    void onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        (void)context;
        (void)stream;
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetCode reason)
    {
        (void)app_context;
        (void)context;
        (void)reason;
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)app_context;
        (void)info;
        (void)code;
        (void)message;
    }

    const KiottyNetConnectionHandler NEVER_READS = { &onConnected, &onReady, &onRead, &onWritten, &onTimeout, &onWorkStarted, &onDisconnected, &onError };

    class StreamClientCancelTest : public ::testing::Test
    {
    protected:
        StreamClientCancelTest()
            : context(nullptr)
            , client(nullptr)
        {
        }

        void SetUp() override
        {
            KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            KiottyNetClientConfiguration configuration;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
            KiottyNetStreamClientResult created = KiottyNet_createStreamClient(context, &configuration);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            client = created.handle;
        }

        void TearDown() override
        {
            KiottyNet_disconnectStreamClient(client);
            KiottyNet_destroyStreamClient(context, client);
            KiottyNet_releaseContext(context);
        }

        void connectTo(kiotty_test::LoopbackPeer& peer)
        {
            ASSERT_TRUE(peer.listenTcp(0));
            const KiottyNetAddress address = loopbackAddress(peer.port());
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectStreamClient(client, &address, nullptr));
            ASSERT_TRUE(peer.accept(WAIT_MS));
        }

        std::string readText(size_t length)
        {
            std::string text;
            char buffer[64] = {};
            while (text.size() < length)
            {
                const KiottyNetResult got = KiottyNet_readStreamClient(client, buffer, length - text.size(), WAIT_MS);
                if (got.code != KIOTTY_NET_CODE_SUCCESS)
                {
                    break;
                }
                text.append(buffer, static_cast<size_t>(got.bytes));
            }
            return text;
        }

        KiottyNetContext            context;
        KiottyNetStreamClientHandle client;
    };
}

TEST_F(StreamClientCancelTest, NullClientIsInvalidArgument)
{
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_cancelStreamClient(nullptr));
}

TEST_F(StreamClientCancelTest, CancelWakesABlockedReadAndTheConnectionStaysOpen)
{
    kiotty_test::LoopbackPeer peer;
    connectTo(peer);
    char buffer[8] = {};
    const Timed woken = cancelDuring(client, [&]() {
        return KiottyNet_readStreamClient(client, buffer, sizeof(buffer), 0);
    });
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, woken.result.code);
    EXPECT_EQ(0, woken.result.bytes);
    EXPECT_LT(woken.elapsed, CANCEL_DELAY_MS + WAKE_BOUND_MS);
    ASSERT_TRUE(peer.sendAll("ok", 2));
    EXPECT_EQ("ok", readText(2));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeStreamClient(client, spanOf("go", 2), WAIT_MS).code);
    char reply[2] = {};
    ASSERT_TRUE(peer.recvExactly(reply, sizeof(reply), WAIT_MS));
    EXPECT_EQ("go", std::string(reply, sizeof(reply)));
}

TEST_F(StreamClientCancelTest, CancelWakesABlockedWriteThatCanGoOnFromWhereItStopped)
{
    kiotty_test::LoopbackPeer peer;
    connectTo(peer);
    const std::vector<char> pattern = patternBytes();
    size_t sent = fillUntilBlocked(client, pattern);
    const Timed woken = cancelDuring(client, [&]() {
        return KiottyNet_writeStreamClient(client, pieceAt(pattern, sent), 0);
    });
    ASSERT_EQ(KIOTTY_NET_CODE_CANCELLED, woken.result.code);
    EXPECT_LT(woken.elapsed, CANCEL_DELAY_MS + WAKE_BOUND_MS);
    EXPECT_LT(static_cast<size_t>(woken.result.bytes), PIECE_BYTES);
    sent += static_cast<size_t>(woken.result.bytes);
    const size_t total = sent + PIECE_BYTES;
    std::atomic<bool> is_received(false);
    std::thread reader([&]() { is_received.store(receivesThePattern(peer, total)); });
    const KiottyNetResult rest = KiottyNet_writeStreamClient(client, pieceAt(pattern, sent), WAIT_MS);
    reader.join();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, rest.code);
    EXPECT_EQ(PIECE_BYTES, static_cast<size_t>(rest.bytes));
    EXPECT_TRUE(is_received.load());
}

TEST_F(StreamClientCancelTest, ACancelWithNothingBlockedIsTakenByTheNextCallOnly)
{
    kiotty_test::LoopbackPeer peer;
    connectTo(peer);
    ASSERT_TRUE(peer.sendAll("hi", 2));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_cancelStreamClient(client));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_cancelStreamClient(client));
    char buffer[8] = {};
    const KiottyNetResult cancelled = KiottyNet_readStreamClient(client, buffer, sizeof(buffer), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, cancelled.code);
    EXPECT_EQ(0, cancelled.bytes);
    EXPECT_EQ("hi", readText(2));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_cancelStreamClient(client));
    const KiottyNetResult refused = KiottyNet_writeStreamClient(client, spanOf("x", 1), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, refused.code);
    EXPECT_EQ(0, refused.bytes);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeStreamClient(client, spanOf("x", 1), WAIT_MS).code);
}

TEST_F(StreamClientCancelTest, DisconnectDropsAPendingCancel)
{
    kiotty_test::LoopbackPeer first;
    connectTo(first);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_cancelStreamClient(client));
    KiottyNet_disconnectStreamClient(client);
    kiotty_test::LoopbackPeer second;
    connectTo(second);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_writeStreamClient(client, spanOf("x", 1), WAIT_MS).code);
}

TEST_F(StreamClientCancelTest, CancelWakesAConnectWaitingBetweenTrials)
{
    KiottyNetAddress nobody = loopbackAddress(kiotty_test::unusedPort());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setAddressRetry(&nobody, 10, 2000));
    const Timed woken = cancelDuring(client, [&]() {
        KiottyNetResult result = { 0, KiottyNet_connectStreamClient(client, &nobody, nullptr) };
        return result;
    });
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, woken.result.code);
    EXPECT_LT(woken.elapsed, CANCEL_DELAY_MS + WAKE_BOUND_MS);
    kiotty_test::LoopbackPeer peer;
    connectTo(peer);
}

TEST_F(StreamClientCancelTest, CppWrapperCancelsTheNextCall)
{
    KiottyNetClientConfiguration configuration;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&configuration));
    KiottyNet_StreamClient cpp(context, configuration);
    ASSERT_TRUE(static_cast<bool>(cpp));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, cpp.cancel());
    char buffer[4] = {};
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, cpp.read(buffer, sizeof(buffer), WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, cpp.read(buffer, sizeof(buffer), WAIT_MS).code);
}

TEST_F(StreamClientCancelTest, CancelWakesATlsHandshake)
{
    if (!kiotty::net::implementation::tlsSupported())
    {
        GTEST_SKIP() << "built without TLS";
    }
    kiotty_test::LoopbackPeer silent;
    ASSERT_TRUE(silent.listenTcp(0));
    KiottyNetAddress address = loopbackAddress(silent.port());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setAddressConnectTimeout(&address, WAIT_MS * 4));
    KiottyNetTlsClientConfiguration tls;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsClientConfiguration(&tls));
    const std::string trust = certPath("ca.pem");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsClientTrustStore(&tls, trust.c_str()));
    const Timed woken = cancelDuring(client, [&]() {
        KiottyNetResult result = { 0, KiottyNet_connectStreamClient(client, &address, &tls) };
        return result;
    });
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, woken.result.code);
    EXPECT_LT(woken.elapsed, CANCEL_DELAY_MS + WAKE_BOUND_MS);
    char buffer[4] = {};
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, KiottyNet_readStreamClient(client, buffer, sizeof(buffer), WAIT_MS).code);
}

TEST_F(StreamClientCancelTest, CancelWakesATlsWriteAndKeepsTheConnection)
{
    if (!kiotty::net::implementation::tlsSupported())
    {
        GTEST_SKIP() << "built without TLS";
    }
    KiottyNetServerConfiguration server_configuration;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&server_configuration));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureServerMaxClients(&server_configuration, 2));
    KiottyNetServerResult made = KiottyNet_createServer(context, &server_configuration);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, made.code);
    KiottyNetTlsServerConfiguration server_tls;
    const std::string cert = certPath("server.pem");
    const std::string key = certPath("server.key");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsServerConfiguration(&server_tls, cert.c_str(), key.c_str(), nullptr));
    const uint16_t port = kiotty_test::unusedPort();
    KiottyNetAddress server_address = loopbackAddress(port);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_setAddressSendTimeout(&server_address, 0));
    std::thread running([&]() { KiottyNet_runServer(made.handle, &server_address, &server_tls, &NEVER_READS, nullptr); });
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitServer(made.handle, WAIT_MS));

    KiottyNetTlsClientConfiguration tls;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTlsClientConfiguration(&tls));
    const std::string trust = certPath("ca.pem");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureTlsClientTrustStore(&tls, trust.c_str()));
    const KiottyNetAddress address = loopbackAddress(port);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectStreamClient(client, &address, &tls));
    const std::vector<char> pattern = patternBytes();
    const size_t sent = fillUntilBlocked(client, pattern);
    const Timed woken = cancelDuring(client, [&]() {
        return KiottyNet_writeStreamClient(client, pieceAt(pattern, sent), 0);
    });
    EXPECT_EQ(KIOTTY_NET_CODE_CANCELLED, woken.result.code);
    EXPECT_LT(woken.elapsed, CANCEL_DELAY_MS + WAKE_BOUND_MS);
    EXPECT_LT(static_cast<size_t>(woken.result.bytes), PIECE_BYTES);
    char buffer[4] = {};
    EXPECT_EQ(KIOTTY_NET_CODE_TIMEOUT, KiottyNet_readStreamClient(client, buffer, sizeof(buffer), 200).code);

    KiottyNet_disconnectStreamClient(client);
    KiottyNet_closeServer(made.handle);
    running.join();
    KiottyNet_destroyServer(context, made.handle);
}
