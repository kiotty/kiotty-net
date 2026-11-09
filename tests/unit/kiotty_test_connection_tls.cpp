#include "data/repository/connection/kiotty_connection.h"
#include "core/kiotty_clock.h"
#include "data/infra/tls/kiotty_tls_pipe.h"
#include "support/kiotty_test_completion.h"
#include "support/kiotty_test_handler.h"
#include "support/kiotty_test_tls.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <utility>

namespace net = kiotty::net::implementation;

using kiotty_test::Call;
using kiotty_test::ConnectionBench;
using kiotty_test::clientConfig;
using kiotty_test::limitsOf;
using kiotty_test::serverConfig;
using kiotty_test::spanOf;
using kiotty_test::thenHold;
using kiotty_test::thenRead;
using kiotty_test::thenWork;
using kiotty_test::thenWriteSpan;
namespace completion = kiotty_test::completion;

#define REQUIRE_TLS()                                            \
    do                                                           \
    {                                                            \
        if (!net::tlsSupported())                                \
        {                                                        \
            GTEST_SKIP() << "built with KIOTTY_NET_USE_TLS=OFF"; \
        }                                                        \
    } while (false)

#define REQUIRE_TLS13()                                          \
    do                                                           \
    {                                                            \
        if (GetParam() != KIOTTY_NET_TLS_VERSION_V13)            \
        {                                                        \
            GTEST_SKIP() << "TLS 1.3 only";                      \
        }                                                        \
    } while (false)

namespace
{
    typedef ConnectionBench<1> Bench;

    const size_t   CAPACITY = 16384;
    const uint32_t READ_TIMEOUT = 5000;
    const int      HANDSHAKE_ROUNDS = 32;

    enum class Side
    {
        SERVER,
        CLIENT
    };

    bool isRead(const net::IoAction& action)
    {
        return action.kind == net::IoActionKind::READ || action.kind == net::IoActionKind::READ_UNTIL;
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

    class TlsLink
    {
    public:
        TlsLink(Side side, uint32_t versions, const char* trust = "ca.pem", size_t receive = CAPACITY)
            : bench(limitsOf(receive, CAPACITY, CAPACITY))
            , peer_profile()
            , peer()
            , connection(nullptr)
            , peer_done(false)
            , _side(side)
            , _versions(versions)
            , _trust(trust)
        {
        }

        void open()
        {
            openProfiles();
            const char* hostname = _side == Side::CLIENT ? "localhost" : "";
            net::ConnectionConfiguration configuration = { net::ActiveSocket(), limitsOf(0, 0, 0), &bench.recorder, hostname, bench.profile };
            net::ConnectionResult acquired = bench.host.acquire(std::move(configuration));
            ASSERT_TRUE(acquired) << acquired.code;
            connection = &acquired.value();
            const char* peer_name = _side == Side::SERVER ? "localhost" : nullptr;
            net::TlsCapacity capacity = { CAPACITY, CAPACITY };
            net::reserveTlsStages(peer.stages(), capacity);
            ASSERT_EQ(peer.open(peer_profile, peer_name, capacity), KIOTTY_NET_CODE_SUCCESS);
            peer_done = false;
        }

        net::IoAction opened()
        {
            return bench.opened(*connection);
        }

        net::IoAction complete(const net::IoCompletion& done)
        {
            return bench.complete(*connection, done);
        }

        net::IoAction pick()
        {
            return bench.pick(*connection);
        }

        size_t peerReadable()
        {
            return peer.stages().send.consumer().readable();
        }

        net::IoAction fromPeer(const net::IoAction& read, size_t limit = static_cast<size_t>(-1))
        {
            net::IoPayload room = connection->resolve(read);
            net::StageBase& source = peer.stages().send;
            size_t length = source.consumer().readable();
            length = length < room.length ? length : room.length;
            length = length < limit ? length : limit;
            std::memcpy(room.buffer, source.consumer().peek(), length);
            source.consumer().commit(length);
            source.compact();
            return complete(completion::receive(read, length));
        }

        net::IoAction toPeer(const net::IoAction& write, size_t* cipher_length = nullptr)
        {
            net::IoPayload payload = connection->resolve(write);
            kiotty_test::put(peer.stages().receive, std::string(static_cast<const char*>(payload.buffer), payload.length));
            if (cipher_length != nullptr)
            {
                *cipher_length = payload.length;
            }
            return complete(completion::sent(write, payload.length));
        }

        net::IoAction exchange(net::IoAction action)
        {
            net::IoAction write = pick();
            if (write.kind == net::IoActionKind::WRITE)
            {
                net::IoAction next = toPeer(write);
                if (next.kind != net::IoActionKind::NONE)
                {
                    action = next;
                }
            }
            if (isRead(action) && peerReadable() > 0)
            {
                action = fromPeer(action);
            }
            return action;
        }

        net::IoAction handshake(net::IoAction action)
        {
            for (int round = 0; round < HANDSHAKE_ROUNDS; ++round)
            {
                stepPeer();
                action = exchange(action);
                const net::Phase phase = connection->state().phase;
                if (phase == net::Phase::CLOSING)
                {
                    return action;
                }
                const bool settled = phase == net::Phase::READY && peer_done && peerReadable() == 0;
                if (settled)
                {
                    return action;
                }
            }
            ADD_FAILURE() << "handshake did not settle";
            return action;
        }

        net::IoAction connect(const KiottyNetConnectionAction& on_ready)
        {
            open();
            bench.recorder.then(on_ready);
            net::IoAction action = handshake(opened());
            EXPECT_EQ(net::Phase::READY, connection->state().phase);
            EXPECT_TRUE(peer_done);
            return action;
        }

        void stepPeer()
        {
            if (peer_done)
            {
                return;
            }
            const net::TlsStep step = peer.handshake();
            peer_done = step == net::TlsStep::DONE;
        }

        void peerSends(const std::string& plain)
        {
            peer.encode(spanOf(plain));
        }

        std::string peerReceives()
        {
            net::Stage plain{ CAPACITY };
            const net::DecodeResult decoded = peer.decode(plain);
            EXPECT_TRUE(decoded) << decoded.code;
            return kiotty_test::take(plain);
        }

        net::IoAction send(const net::IoAction& write)
        {
            EXPECT_EQ(net::IoActionKind::WRITE, write.kind);
            return toPeer(write);
        }

        Bench           bench;
        net::TlsProfile peer_profile;
        net::TlsEngine  peer;
        net::Connection* connection;
        bool            peer_done;

    private:
        void openProfiles()
        {
            KiottyNetTlsServerConfiguration server = serverConfig();
            KiottyNetTlsClientConfiguration client = clientConfig(_trust);
            server.versions = _versions;
            client.versions = _versions;
            net::TlsProfile& server_profile = _side == Side::SERVER ? bench.profile : peer_profile;
            net::TlsProfile& client_profile = _side == Side::SERVER ? peer_profile : bench.profile;
            ASSERT_EQ(server_profile.openServer(server), KIOTTY_NET_CODE_SUCCESS);
            ASSERT_EQ(client_profile.openClient(client), KIOTTY_NET_CODE_SUCCESS);
        }

        Side        _side;
        uint32_t    _versions;
        const char* _trust;
    };

    class ConnectionTls : public ::testing::TestWithParam<uint32_t>
    {
    protected:
        void SetUp() override
        {
            REQUIRE_TLS();
        }
    };

    std::string versionName(const ::testing::TestParamInfo<uint32_t>& info)
    {
        return info.param == KIOTTY_NET_TLS_VERSION_V13 ? "Tls13" : "Tls12";
    }
}

INSTANTIATE_TEST_SUITE_P(Version, ConnectionTls,
    ::testing::Values(static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V12), static_cast<uint32_t>(KIOTTY_NET_TLS_VERSION_V13)),
    versionName);

TEST_P(ConnectionTls, OnOpenedTlsServerReturnsHandshakeReadWithoutCallingOnReady)
{
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    const uint64_t before = net::monotonicMilliseconds();
    net::IoAction action = link.opened();
    const uint64_t after = net::monotonicMilliseconds();
    EXPECT_EQ(net::IoActionKind::READ, action.kind);
    EXPECT_EQ(net::IoPurpose::HANDSHAKE, action.purpose);
    EXPECT_EQ(0u, action.as.read.exact);
    EXPECT_EQ(net::TLS_HANDSHAKE_TIMEOUT_MILLISECONDS, action.timeout_milliseconds);
    EXPECT_EQ(0u, link.bench.recorder.total());
    EXPECT_EQ(net::Phase::HANDSHAKING, link.connection->state().phase);
    EXPECT_GE(link.connection->state().deadline, before + net::TLS_HANDSHAKE_TIMEOUT_MILLISECONDS);
    EXPECT_LE(link.connection->state().deadline, after + net::TLS_HANDSHAKE_TIMEOUT_MILLISECONDS);
}

TEST_P(ConnectionTls, OnOpenedTlsClientReturnsNoneAndLeavesClientHelloForDrain)
{
    TlsLink link(Side::CLIENT, GetParam());
    link.open();
    const uint64_t before = net::monotonicMilliseconds();
    net::IoAction action = link.opened();
    EXPECT_EQ(net::IoActionKind::NONE, action.kind);
    EXPECT_EQ(0u, link.bench.recorder.total());
    EXPECT_GE(link.connection->state().deadline, before + net::TLS_HANDSHAKE_TIMEOUT_MILLISECONDS);
    net::IoAction write = link.pick();
    EXPECT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::HANDSHAKE, write.purpose);
    EXPECT_EQ(0u, write.as.write.length);
    EXPECT_GT(link.connection->resolve(write).length, 0u);
}

TEST_P(ConnectionTls, HandshakeReceiveThatNeedsToSendReturnsNoneAndLeavesBytesForDrain)
{
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    net::IoAction read = link.opened();
    link.stepPeer();
    ASSERT_GT(link.peerReadable(), 0u);
    net::IoAction action = link.fromPeer(read);
    EXPECT_EQ(net::IoActionKind::NONE, action.kind);
    net::IoAction write = link.pick();
    EXPECT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::HANDSHAKE, write.purpose);
    EXPECT_GT(link.connection->resolve(write).length, 0u);
}

TEST_P(ConnectionTls, SendHandshakeConsumesCipherSendByCompletionLengthAndContinuesHandshake)
{
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    net::IoAction read = link.opened();
    link.stepPeer();
    link.fromPeer(read);
    net::IoAction write = link.pick();
    ASSERT_EQ(net::IoActionKind::WRITE, write.kind);
    const size_t flight = link.connection->resolve(write).length;
    ASSERT_GT(flight, 1u);
    net::IoAction partial = link.complete(completion::sent(write, 1));
    EXPECT_EQ(net::IoActionKind::NONE, partial.kind);
    EXPECT_EQ(flight - 1, link.connection->resolve(write).length);
    net::IoAction next = link.complete(completion::sent(write, flight - 1));
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(net::IoPurpose::HANDSHAKE, next.purpose);
    EXPECT_EQ(0u, link.connection->resolve(write).length);
}

TEST_P(ConnectionTls, Tls13HandshakeIsDoneRightAfterClientFinishedWithoutATicket)
{
    REQUIRE_TLS13();
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    link.bench.recorder.then(thenHold(0));
    net::IoAction read = link.opened();
    link.stepPeer();
    link.fromPeer(read);
    net::IoAction awaiting = link.toPeer(link.pick());
    ASSERT_EQ(net::IoPurpose::HANDSHAKE, awaiting.purpose);
    link.stepPeer();
    ASSERT_GT(link.peerReadable(), 0u);
    EXPECT_EQ(0u, link.bench.recorder.count(Call::ON_READY));
    net::IoAction ready = link.fromPeer(awaiting);
    EXPECT_EQ(net::IoActionKind::HOLD, ready.kind);
    EXPECT_EQ(net::Phase::READY, link.connection->state().phase);
    EXPECT_EQ(1u, link.bench.recorder.count(Call::ON_READY));
    EXPECT_EQ(net::IoActionKind::NONE, link.pick().kind);
}

TEST_P(ConnectionTls, PickDuringHandshakingOffersOnlyCipherSendAndNotifyIsAccepted)
{
    TlsLink link(Side::CLIENT, GetParam());
    link.open();
    link.bench.recorder.then(thenHold(0));
    net::IoAction action = link.opened();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, link.bench.notify(*link.connection, "early"));
    net::IoAction first = link.pick();
    EXPECT_EQ(net::IoPurpose::HANDSHAKE, first.purpose);
    action = link.toPeer(first);
    EXPECT_EQ(net::IoActionKind::NONE, link.pick().kind);
    for (int round = 0; round < HANDSHAKE_ROUNDS && link.connection->state().phase == net::Phase::HANDSHAKING; ++round)
    {
        link.stepPeer();
        net::IoAction write = link.pick();
        if (write.kind == net::IoActionKind::WRITE)
        {
            EXPECT_EQ(net::IoPurpose::HANDSHAKE, write.purpose);
            net::IoAction next = link.toPeer(write);
            if (next.kind != net::IoActionKind::NONE)
            {
                action = next;
            }
        }
        if (isRead(action) && link.peerReadable() > 0)
        {
            action = link.fromPeer(action);
        }
    }
    ASSERT_EQ(net::Phase::READY, link.connection->state().phase);
    net::IoAction write = link.pick();
    while (write.purpose == net::IoPurpose::HANDSHAKE || write.purpose == net::IoPurpose::CONTROL)
    {
        link.toPeer(write);
        write = link.pick();
    }
    EXPECT_EQ(net::IoActionKind::WRITE, write.kind);
    EXPECT_EQ(net::IoPurpose::NOTIFY, write.purpose);
    EXPECT_EQ(5u, write.as.write.length);
}

TEST_P(ConnectionTls, PickPutsCipherSendBeforeNotifyAndSend)
{
    REQUIRE_TLS13();
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    net::TlsPipe(link.peer).requestKeyUpdate();
    std::string reply("pong");
    link.bench.recorder.thenWrite(reply, READ_TIMEOUT);
    link.peerSends("ping");
    link.fromPeer(read);
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_READ));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, link.bench.notify(*link.connection, "note"));
    net::IoAction control = link.pick();
    EXPECT_EQ(net::IoPurpose::CONTROL, control.purpose);
    EXPECT_EQ(0u, control.as.write.length);
    EXPECT_EQ(net::IoActionKind::NONE, link.send(control).kind);
    net::IoAction notify = link.pick();
    EXPECT_EQ(net::IoPurpose::NOTIFY, notify.purpose);
    link.send(notify);
    net::IoAction user = link.pick();
    EXPECT_EQ(net::IoPurpose::USER, user.purpose);
    link.send(user);
    EXPECT_EQ("notepong", link.peerReceives());
}

TEST_P(ConnectionTls, TimeoutDuringHandshakeReportsHandshakeTimeoutAndCloses)
{
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    link.opened();
    net::IoAction action = link.complete(completion::timeout());
    EXPECT_EQ(net::IoActionKind::CLOSE, action.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT, action.as.close.reason);
    EXPECT_EQ(0u, link.bench.recorder.count(Call::ON_TIMEOUT));
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_ERROR));
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT, link.bench.recorder.last(Call::ON_ERROR).code);
    EXPECT_STREQ("tls: handshake timeout", link.bench.recorder.last(Call::ON_ERROR).message);
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT, link.bench.recorder.last(Call::ON_DISCONNECTED).code);
    EXPECT_EQ(net::Phase::CLOSING, link.connection->state().phase);
}

TEST_P(ConnectionTls, HandshakeFailureReportsEngineCodeAndMessageThenCloses)
{
    TlsLink link(Side::CLIENT, GetParam(), "other_ca.pem");
    link.open();
    net::IoAction action = link.handshake(link.opened());
    EXPECT_EQ(net::IoActionKind::CLOSE, action.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_VERIFY_FAILED, action.as.close.reason);
    EXPECT_EQ(0u, link.bench.recorder.count(Call::ON_READY));
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_ERROR));
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_VERIFY_FAILED, link.bench.recorder.last(Call::ON_ERROR).code);
    ASSERT_NE(nullptr, link.bench.recorder.last(Call::ON_ERROR).message);
    EXPECT_GT(std::strlen(link.bench.recorder.last(Call::ON_ERROR).message), 0u);
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_VERIFY_FAILED, link.bench.recorder.last(Call::ON_DISCONNECTED).code);
}

TEST_P(ConnectionTls, ApplicationDataArrivingWithFinishedIsDeliveredByRefillAtOnReady)
{
    REQUIRE_TLS13();
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    link.bench.recorder.then(thenRead(4, READ_TIMEOUT));
    net::IoAction read = link.opened();
    link.stepPeer();
    link.fromPeer(read);
    net::IoAction awaiting = link.toPeer(link.pick());
    link.stepPeer();
    link.peerSends("ping");
    net::IoAction next = link.fromPeer(awaiting);
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_READY));
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("ping", link.bench.recorder.last(Call::ON_READ).data);
    EXPECT_EQ(net::IoActionKind::HOLD, next.kind);
}

TEST_P(ConnectionTls, TlsReceiveDecodesInRefillAndDeliversPlaintext)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    ASSERT_EQ(net::IoActionKind::READ, read.kind);
    EXPECT_EQ(net::IoPurpose::USER, read.purpose);
    link.peerSends("ping");
    net::IoAction next = link.fromPeer(read);
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("ping", link.bench.recorder.last(Call::ON_READ).data);
    EXPECT_EQ(net::IoActionKind::HOLD, next.kind);
}

TEST_P(ConnectionTls, TlsReceiveWithHalfRecordReturnsTheSameReadAndNoCallback)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    link.peerSends("ping");
    net::IoAction next = link.fromPeer(read, 3);
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(4u, next.as.read.exact);
    EXPECT_EQ(0u, link.bench.recorder.count(Call::ON_READ));
    link.fromPeer(next);
    EXPECT_EQ(1u, link.bench.recorder.count(Call::ON_READ));
}

TEST_P(ConnectionTls, PlaintextLeftInSslIsDrainedByRefillBeforeReadIsReturned)
{
    const size_t piece = 4096;
    TlsLink link(Side::SERVER, GetParam(), "ca.pem", piece);
    net::IoAction read = link.connect(thenRead(piece, READ_TIMEOUT));
    for (int i = 0; i < 4; ++i)
    {
        link.bench.recorder.then(thenRead(piece, READ_TIMEOUT));
    }
    link.peerSends(pattern(4 * piece));
    int deliveries = 0;
    while (link.bench.recorder.count(Call::ON_READ) == 0 && deliveries < 16)
    {
        read = link.fromPeer(read);
        ++deliveries;
    }
    EXPECT_GT(deliveries, 1);
    EXPECT_EQ(4u, link.bench.recorder.count(Call::ON_READ));
    EXPECT_EQ(net::IoActionKind::READ, read.kind);
    EXPECT_EQ(piece, read.as.read.exact);
}

TEST_P(ConnectionTls, TlsReceiveWithCorruptRecordReportsRecordErrorAndCloses)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    link.peerSends("ping");
    net::StageBase& sent = link.peer.stages().send;
    const size_t length = sent.consumer().readable();
    sent.consumer().peek()[length - 1] ^= 0x5A;
    net::IoAction next = link.fromPeer(read);
    EXPECT_EQ(net::IoActionKind::CLOSE, next.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_RECORD_ERROR, next.as.close.reason);
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_ERROR));
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_RECORD_ERROR, link.bench.recorder.last(Call::ON_ERROR).code);
    EXPECT_EQ(KIOTTY_NET_CODE_TLS_RECORD_ERROR, link.bench.recorder.last(Call::ON_DISCONNECTED).code);
    EXPECT_EQ(0u, link.bench.recorder.count(Call::ON_READ));
}

TEST_P(ConnectionTls, CloseNotifyFromPeerClosesWithPeerClosed)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    net::TlsPipe(link.peer).sendCloseNotify();
    net::IoAction next = link.fromPeer(read);
    EXPECT_EQ(net::IoActionKind::CLOSE, next.kind);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, next.as.close.reason);
    EXPECT_EQ(KIOTTY_NET_CODE_PEER_CLOSED, link.bench.recorder.last(Call::ON_DISCONNECTED).code);
}

TEST_P(ConnectionTls, WriteLengthIsPlaintextAndCompletionLengthIsCiphertext)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    std::string reply("hello");
    link.bench.recorder.thenWrite(reply, READ_TIMEOUT);
    link.bench.recorder.then(thenHold(0));
    link.peerSends("ping");
    link.fromPeer(read);
    net::IoAction write = link.pick();
    ASSERT_EQ(net::IoPurpose::USER, write.purpose);
    EXPECT_EQ(reply.size(), write.as.write.length);
    size_t cipher = 0;
    link.toPeer(write, &cipher);
    EXPECT_GT(cipher, reply.size());
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_WRITTEN));
    EXPECT_EQ(reply.size(), link.bench.recorder.last(Call::ON_WRITTEN).length);
    EXPECT_EQ(0u, link.connection->resolve(write).length);
    EXPECT_EQ(net::IoActionKind::NONE, link.pick().kind);
    EXPECT_EQ(reply, link.peerReceives());
}

TEST_P(ConnectionTls, PickedContextIsEncodedAtPickTimeNotAtStage)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    std::string reply("hello");
    link.bench.recorder.thenWrite(reply, READ_TIMEOUT);
    link.peerSends("ping");
    link.fromPeer(read);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, link.bench.notify(*link.connection, "note"));
    const net::IoAction probe = net::IoAction::write(net::IoPurpose::USER, 0, 0);
    EXPECT_EQ(0u, link.connection->resolve(probe).length);
    net::IoAction write = link.pick();
    EXPECT_GT(link.connection->resolve(probe).length, 0u);
    EXPECT_EQ(net::IoPurpose::NOTIFY, write.purpose);
}

TEST_P(ConnectionTls, EncodeOrderEqualsSendOrderNotifyRecordFirst)
{
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    std::string reply("reply");
    link.bench.recorder.thenWrite(reply, READ_TIMEOUT);
    link.peerSends("ping");
    link.fromPeer(read);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, link.bench.notify(*link.connection, "notify"));
    net::IoAction first = link.pick();
    EXPECT_EQ(net::IoPurpose::NOTIFY, first.purpose);
    link.send(first);
    net::IoAction second = link.pick();
    EXPECT_EQ(net::IoPurpose::USER, second.purpose);
    link.send(second);
    EXPECT_EQ("notifyreply", link.peerReceives());
}

TEST_P(ConnectionTls, KeyUpdateReplyStaysInCipherSendAndNextPickSendsItAsControl)
{
    REQUIRE_TLS13();
    TlsLink link(Side::SERVER, GetParam());
    net::IoAction read = link.connect(thenRead(4, READ_TIMEOUT));
    net::TlsPipe(link.peer).requestKeyUpdate();
    net::IoAction next = link.fromPeer(read);
    EXPECT_EQ(net::IoActionKind::READ, next.kind);
    EXPECT_EQ(0u, link.bench.recorder.count(Call::ON_READ));
    net::IoAction control = link.pick();
    EXPECT_EQ(net::IoActionKind::WRITE, control.kind);
    EXPECT_EQ(net::IoPurpose::CONTROL, control.purpose);
    EXPECT_EQ(net::IoActionKind::NONE, link.send(control).kind);
    EXPECT_EQ("", link.peerReceives());

    std::string reply("after");
    link.bench.recorder.thenWrite(reply, READ_TIMEOUT);
    link.peerSends("ping");
    link.fromPeer(next);
    ASSERT_EQ(1u, link.bench.recorder.count(Call::ON_READ));
    EXPECT_EQ("ping", link.bench.recorder.last(Call::ON_READ).data);
    link.send(link.pick());
    EXPECT_EQ(reply, link.peerReceives());
}

TEST_P(ConnectionTls, CloseStreamWithThenWriteIsEncodedAtPickNotByTheWorker)
{
    TlsLink link(Side::SERVER, GetParam());
    link.connect(thenWork());
    ASSERT_TRUE(link.connection->flows().work.isReady());
    link.complete(completion::workReady());
    net::WorkFlow& work = link.connection->flows().work;
    const int32_t generation = link.connection->state().id.generation;
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, work.verify(generation));
    std::string reply("done");
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, work.closeStream(generation, thenWriteSpan(spanOf(reply), READ_TIMEOUT)));
    const net::IoAction probe = net::IoAction::write(net::IoPurpose::USER, 0, 0);
    EXPECT_EQ(0u, link.connection->resolve(probe).length);
    net::IoAction outcome = work.takeOutcome();
    EXPECT_EQ(net::IoActionKind::HOLD, link.complete(completion::workDone(outcome)).kind);
    net::IoAction write = link.pick();
    EXPECT_EQ(net::IoPurpose::USER, write.purpose);
    EXPECT_GT(link.connection->resolve(probe).length, reply.size());
    link.send(write);
    EXPECT_EQ(reply, link.peerReceives());
}

TEST_P(ConnectionTls, ReceiveStageIsCipherReceiveForHandshakeAndTlsUser)
{
    TlsLink link(Side::SERVER, GetParam());
    link.open();
    link.opened();
    const net::IoAction handshake = net::IoAction::read(net::IoPurpose::HANDSHAKE, 0, 0);
    const net::IoAction user = net::IoAction::read(net::IoPurpose::USER, 0, 1);
    net::IoPayload during = link.connection->resolve(handshake);
    EXPECT_EQ(during.buffer, link.connection->resolve(user).buffer);
    EXPECT_EQ(CAPACITY, during.length);

    TlsLink ready(Side::SERVER, GetParam());
    ready.connect(thenHold(0));
    net::IoPayload after = ready.connection->resolve(user);
    EXPECT_EQ(after.buffer, ready.connection->resolve(handshake).buffer);
    EXPECT_EQ(after.length, ready.connection->resolve(handshake).length);
    EXPECT_GT(after.length, 0u);
}

TEST_P(ConnectionTls, ResolveWriteOnTlsPointsAtCipherSendReadable)
{
    TlsLink link(Side::SERVER, GetParam());
    link.connect(thenHold(0));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, link.bench.notify(*link.connection, "note"));
    net::IoAction write = link.pick();
    ASSERT_EQ(net::IoPurpose::NOTIFY, write.purpose);
    net::IoPayload notify = link.connection->resolve(write);
    const net::IoPurpose purposes[] = { net::IoPurpose::USER, net::IoPurpose::HANDSHAKE, net::IoPurpose::CONTROL };
    for (net::IoPurpose purpose : purposes)
    {
        net::IoPayload other = link.connection->resolve(net::IoAction::write(purpose, 0, 0));
        EXPECT_EQ(notify.buffer, other.buffer);
        EXPECT_EQ(notify.length, other.length);
    }
    EXPECT_GT(notify.length, write.as.write.length);
}

TEST_P(ConnectionTls, ReleaseThenOpenReusesTheEngineAndHandshakesAgain)
{
    TlsLink link(Side::SERVER, GetParam());
    link.connect(thenHold(0));
    const net::PoolHandle first = link.connection->state().id;
    link.bench.host.release(*link.connection);
    EXPECT_EQ(net::Phase::IDLE, link.bench.host.at(0).state().phase);

    link.peer.close();
    link.bench.profile.close();
    link.peer_profile.close();
    link.connect(thenHold(0));
    EXPECT_EQ(first.index, link.connection->state().id.index);
    EXPECT_NE(first.generation, link.connection->state().id.generation);
    EXPECT_EQ(2u, link.bench.recorder.count(Call::ON_READY));
}
