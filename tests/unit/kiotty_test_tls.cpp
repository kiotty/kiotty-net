#include "support/kiotty_test_openssl_memory.h"
#include "support/kiotty_test_tls.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using kiotty::net::implementation::Stage;
using kiotty::net::implementation::StageBase;
using kiotty::net::implementation::MemoryLimits;
using kiotty::net::implementation::DecodeResult;
using kiotty::net::implementation::TlsEngine;
using kiotty::net::implementation::TlsProfile;
using kiotty::net::implementation::TlsStep;
using kiotty_test::OpenSslMemory;
using kiotty_test::TlsPump;
using kiotty_test::certPath;
using kiotty_test::clientConfig;
using kiotty_test::copyText;
using kiotty_test::move;
using kiotty_test::openSslMemory;
using kiotty_test::put;
using kiotty_test::serverConfig;
using kiotty_test::take;

#define REQUIRE_TLS()                                       \
    do                                                      \
    {                                                       \
        if (!kiotty::net::implementation::tlsSupported())                   \
        {                                                   \
            GTEST_SKIP() << "built with KIOTTY_NET_USE_TLS=OFF"; \
        }                                                   \
    } while (false)

namespace
{
    KiottyNetSpan spanOf(const std::string& text)
    {
        KiottyNetSpan span = { text.data(), text.size() };
        return span;
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

    struct Profiles
    {
        TlsProfile server;
        TlsProfile client;
    };

    void openDefaults(Profiles& profiles)
    {
        ASSERT_EQ(profiles.server.openServer(serverConfig()), KIOTTY_SUCCESS);
        ASSERT_EQ(profiles.client.openClient(clientConfig()), KIOTTY_SUCCESS);
    }

    void connect(TlsPump& pump, Profiles& profiles)
    {
        ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
        pump.handshake();
        ASSERT_TRUE(pump.done());
        Stage<16> plain;
        DecodeResult swallowed = pump.client.decode(plain);
        ASSERT_TRUE(swallowed);
        ASSERT_EQ(swallowed.value, 0u);
    }

    std::string roundTrip(TlsEngine& from, TlsEngine& to, const std::string& text, size_t plain_capacity)
    {
        from.encode(spanOf(text));
        std::string received;
        Stage<kiotty::net::implementation::MemoryLimits::RECEIVE_CAPACITY> plain;
        plain.limit(plain_capacity);
        while (from.cipherSend().consumer().readable() > 0)
        {
            move(from, to);
            DecodeResult result = to.decode(plain);
            if (!result)
            {
                break;
            }
            received += take(plain);
        }
        return received;
    }
}

TEST(TlsProfile, OpenServerWithValidCertAndKeySucceeds)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig()), KIOTTY_SUCCESS);
    EXPECT_TRUE(profile.valid());
    EXPECT_TRUE(profile.server());
}

TEST(TlsProfile, OpenServerWithMissingFileReturnsCertificateNotFound)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig("missing.pem", "server.key")), KIOTTY_TLS_CERTIFICATE_NOT_FOUND);
    EXPECT_FALSE(profile.valid());
}

TEST(TlsProfile, OpenServerWithKeyThatDoesNotMatchCertReturnsCertificateInvalid)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig("server.pem", "client.key")), KIOTTY_TLS_CERTIFICATE_INVALID);
    EXPECT_FALSE(profile.valid());
}

TEST(TlsProfile, OpenServerWithGarbageFileReturnsCertificateInvalid)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig("garbage.pem", "server.key")), KIOTTY_TLS_CERTIFICATE_INVALID);
    EXPECT_FALSE(profile.valid());
}

TEST(TlsProfile, OpenClientWithDefaultsSucceedsAndHandshakeTimeoutIsTheDefault)
{
    REQUIRE_TLS();
    KiottyNetTlsClientConfig config = clientConfig();
    config.trust_store_path[0] = '\0';
    TlsProfile profile;
    EXPECT_EQ(profile.openClient(config), KIOTTY_SUCCESS);
    EXPECT_FALSE(profile.server());
    EXPECT_TRUE(profile.verifyHostname());
    EXPECT_EQ(profile.handshakeTimeoutMs(), kiotty::net::implementation::TLS_HANDSHAKE_TIMEOUT_MS);
}

TEST(TlsProfile, StubBuildReturnsNotSupportedForBothOpens)
{
    if (kiotty::net::implementation::tlsSupported())
    {
        GTEST_SKIP() << "built with KIOTTY_NET_USE_TLS=ON";
    }
    TlsProfile server;
    TlsProfile client;
    EXPECT_EQ(server.openServer(serverConfig()), KIOTTY_TLS_NOT_SUPPORTED);
    EXPECT_EQ(client.openClient(clientConfig()), KIOTTY_TLS_NOT_SUPPORTED);
    EXPECT_FALSE(server.valid());
    EXPECT_FALSE(client.valid());
    TlsEngine engine;
    engine.limit(4096, 4096);
    EXPECT_EQ(engine.open(server, nullptr), KIOTTY_TLS_NOT_SUPPORTED);
    EXPECT_FALSE(engine.active());
}

TEST(TlsEngine, InactiveEngineReportsActiveFalseAndAllocatesNoStage)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsEngine engine;
    engine.limit(0, 0);
    EXPECT_FALSE(engine.active());
    EXPECT_EQ(engine.cipherReceive().capacity(), 0u);
    EXPECT_EQ(engine.cipherSend().capacity(), 0u);
    EXPECT_EQ(engine.open(profiles.server, nullptr), KIOTTY_INVALID_ARGUMENT);
    EXPECT_FALSE(engine.active());
}

TEST(TlsEngine, ServerFirstStepIsReceiveAndClientFirstStepIsSend)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    EXPECT_EQ(pump.server.handshake(), TlsStep::RECEIVE);
    EXPECT_EQ(pump.server.cipherSend().consumer().readable(), 0u);
    EXPECT_EQ(pump.client.handshake(), TlsStep::SEND);
    EXPECT_GT(pump.client.cipherSend().consumer().readable(), 0u);
}

TEST(TlsEngine, PumpingCipherBytesBetweenClientAndServerReachesDoneOnBoth)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    int rounds = pump.handshake();
    EXPECT_TRUE(pump.done());
    EXPECT_LE(rounds, 6);
}

TEST(TlsEngine, Tls12OnlyProfilesAlsoReachDone)
{
    REQUIRE_TLS();
    KiottyNetTlsServerConfig server_config = serverConfig();
    KiottyNetTlsClientConfig client_config = clientConfig();
    server_config.versions = KIOTTY_TLS_V12;
    client_config.versions = KIOTTY_TLS_V12;
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(server_config), KIOTTY_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(client_config), KIOTTY_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    pump.handshake();
    EXPECT_TRUE(pump.done());
    EXPECT_EQ(roundTrip(pump.client, pump.server, "tls 1.2", 64), "tls 1.2");
}

TEST(TlsEngine, EncodeOnOneSideDecodesToTheSamePlaintextOnTheOther)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    const size_t lengths[] = { 1, 4096, 16383 };
    for (size_t length : lengths)
    {
        std::string text = pattern(length);
        EXPECT_EQ(roundTrip(pump.client, pump.server, text, 16384), text) << length;
        EXPECT_EQ(roundTrip(pump.server, pump.client, text, 16384), text) << length;
    }
}

TEST(TlsEngine, EncodeLargerThanOneRecordProducesMultipleRecordsThatDecodeInOrder)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    if (MemoryLimits::SEND_CAPACITY <= kiotty::net::implementation::TLS_RECORD_PLAIN_BYTES || MemoryLimits::RECEIVE_CAPACITY <= kiotty::net::implementation::TLS_RECORD_PLAIN_BYTES)
    {
        GTEST_SKIP() << "one plain message fits a single record in this build";
    }
    TlsPump pump;
    connect(pump, profiles);
    std::string text = pattern(MemoryLimits::SEND_CAPACITY);
    pump.client.encode(spanOf(text));
    EXPECT_GT(pump.client.cipherSend().consumer().readable(), text.size() + 2 * 5);
    EXPECT_EQ(roundTrip(pump.client, pump.server, "", MemoryLimits::RECEIVE_CAPACITY), text);
}

TEST(TlsEngine, DecodeWithHalfARecordYieldsZeroBytesAndNoError)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    std::string text = pattern(1000);
    pump.client.encode(spanOf(text));
    size_t record = pump.client.cipherSend().consumer().readable();
    Stage<4096> plain;
    move(pump.client, pump.server, record / 2);
    DecodeResult first = pump.server.decode(plain);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value, 0u);
    move(pump.client, pump.server);
    DecodeResult second = pump.server.decode(plain);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value, text.size());
    EXPECT_EQ(take(plain), text);
}

TEST(TlsEngine, DecodeWithOneByteAtATimeEventuallyYieldsTheRecord)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    std::string text = pattern(300);
    pump.client.encode(spanOf(text));
    Stage<4096> plain;
    std::string received;
    while (pump.client.cipherSend().consumer().readable() > 0)
    {
        move(pump.client, pump.server, 1);
        DecodeResult result = pump.server.decode(plain);
        ASSERT_TRUE(result);
        received += take(plain);
    }
    EXPECT_EQ(received, text);
}

TEST(TlsEngine, DecodeOfCorruptedRecordReturnsRecordErrorWithNonEmptyMessage)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    pump.client.encode(spanOf(std::string("hello")));
    StageBase& cipher = pump.client.cipherSend();
    size_t length = cipher.consumer().readable();
    cipher.consumer().peek()[length - 1] ^= 0x01;
    move(pump.client, pump.server);
    Stage<4096> plain;
    DecodeResult result = pump.server.decode(plain);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.code, KIOTTY_TLS_RECORD_ERROR);
    EXPECT_EQ(pump.server.lastCode(), KIOTTY_TLS_RECORD_ERROR);
    EXPECT_STRNE(pump.server.lastMessage(), "");
}

TEST(TlsEngine, ClientRejectsServerSignedByUntrustedCaWithVerifyFailed)
{
    REQUIRE_TLS();
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig()), KIOTTY_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(clientConfig("other_ca.pem")), KIOTTY_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.client_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.client.lastCode(), KIOTTY_TLS_VERIFY_FAILED);
    EXPECT_STRNE(pump.client.lastMessage(), "");
}

TEST(TlsEngine, ClientWithVerifyServerOffAcceptsUntrustedCa)
{
    REQUIRE_TLS();
    KiottyNetTlsClientConfig config = clientConfig("other_ca.pem");
    config.skip_server_verification = 1;
    config.skip_hostname_verification = 1;
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig()), KIOTTY_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(config), KIOTTY_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    pump.handshake();
    EXPECT_TRUE(pump.done());
}

TEST(TlsEngine, HostnameMismatchReturnsHostnameMismatch)
{
    REQUIRE_TLS();
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig("server_wrong_host.pem", "server_wrong_host.key")), KIOTTY_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(clientConfig()), KIOTTY_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client, "localhost"), KIOTTY_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.client_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.client.lastCode(), KIOTTY_TLS_HOSTNAME_MISMATCH);
}

TEST(TlsEngine, ExpiredServerCertReturnsVerifyFailed)
{
    REQUIRE_TLS();
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig("server_expired.pem", "server_expired.key")), KIOTTY_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(clientConfig()), KIOTTY_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.client_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.client.lastCode(), KIOTTY_TLS_VERIFY_FAILED);
}

TEST(TlsEngine, ServerRequiringClientAuthFailsWithoutClientCertAndSucceedsWithIt)
{
    REQUIRE_TLS();
    KiottyNetTlsServerConfig server_config = serverConfig();
    server_config.client_auth = KIOTTY_CLIENT_AUTH_REQUIRED;
    copyText(server_config.trust_store_path, certPath("ca.pem"));
    TlsProfile server;
    ASSERT_EQ(server.openServer(server_config), KIOTTY_SUCCESS);

    TlsProfile anonymous;
    ASSERT_EQ(anonymous.openClient(clientConfig()), KIOTTY_SUCCESS);
    TlsPump rejected;
    ASSERT_EQ(rejected.open(server, anonymous), KIOTTY_SUCCESS);
    rejected.handshake();
    ASSERT_EQ(rejected.server_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(rejected.server.lastCode(), KIOTTY_TLS_VERIFY_FAILED);

    KiottyNetTlsClientConfig client_config = clientConfig();
    copyText(client_config.identity.cert_file, certPath("client.pem"));
    copyText(client_config.identity.key_file, certPath("client.key"));
    TlsProfile identified;
    ASSERT_EQ(identified.openClient(client_config), KIOTTY_SUCCESS);
    TlsPump accepted;
    ASSERT_EQ(accepted.open(server, identified), KIOTTY_SUCCESS);
    accepted.handshake();
    EXPECT_TRUE(accepted.done());
}

TEST(TlsEngine, VersionMismatchReturnsHandshakeFailed)
{
    REQUIRE_TLS();
    KiottyNetTlsServerConfig server_config = serverConfig();
    KiottyNetTlsClientConfig client_config = clientConfig();
    server_config.versions = KIOTTY_TLS_V13;
    client_config.versions = KIOTTY_TLS_V12;
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(server_config), KIOTTY_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(client_config), KIOTTY_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.server_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.server.lastCode(), KIOTTY_TLS_HANDSHAKE_FAILED);
}

TEST(TlsEngine, PlaintextBytesFromPeerReturnHandshakeFailed)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsEngine server;
    server.limit(4096, 4096);
    ASSERT_EQ(server.open(profiles.server, nullptr), KIOTTY_SUCCESS);
    put(server.cipherReceive(), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_EQ(server.handshake(), TlsStep::FAILED);
    EXPECT_EQ(server.lastCode(), KIOTTY_TLS_HANDSHAKE_FAILED);
}

TEST(TlsEngine, Tls13ServerSendsNoSessionTicketSoItIsDoneRightAfterTheClientFinished)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
    EXPECT_EQ(pump.client.handshake(), TlsStep::SEND);
    move(pump.client, pump.server);
    EXPECT_EQ(pump.server.handshake(), TlsStep::SEND);
    move(pump.server, pump.client);
    EXPECT_EQ(pump.client.handshake(), TlsStep::SEND);
    move(pump.client, pump.server);
    EXPECT_EQ(pump.server.handshake(), TlsStep::DONE);
    EXPECT_EQ(pump.server.cipherSend().consumer().readable(), 0u);
    EXPECT_EQ(pump.client.cipherReceive().consumer().readable(), 0u);
    EXPECT_EQ(pump.client.handshake(), TlsStep::DONE);
}

TEST(TlsEngine, KeyUpdateFromPeerLeavesTheReplyInCipherSendAfterDecode)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    pump.client.debugKeyUpdate();
    ASSERT_GT(pump.client.cipherSend().consumer().readable(), 0u);
    move(pump.client, pump.server);
    Stage<4096> plain;
    DecodeResult result = pump.server.decode(plain);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value, 0u);
    EXPECT_GT(pump.server.cipherSend().consumer().readable(), 0u);
    move(pump.server, pump.client);
    DecodeResult reply = pump.client.decode(plain);
    ASSERT_TRUE(reply);
    EXPECT_EQ(roundTrip(pump.client, pump.server, "after update", 64), "after update");
    EXPECT_EQ(roundTrip(pump.server, pump.client, "both ways", 64), "both ways");
}

TEST(TlsEngine, CloseNotifyFromPeerDecodesAsPeerClosed)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    pump.client.debugShutdown();
    move(pump.client, pump.server);
    Stage<4096> plain;
    DecodeResult result = pump.server.decode(plain);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.code, KIOTTY_PEER_CLOSED);
}

TEST(TlsEngine, CloseThenOpenReusesTheSslObjectAndHandshakesAgain)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    EXPECT_EQ(roundTrip(pump.client, pump.server, "first", 64), "first");
    pump.server.close();
    pump.client.close();
    EXPECT_FALSE(pump.server.active());
    connect(pump, profiles);
    EXPECT_EQ(roundTrip(pump.client, pump.server, "second", 64), "second");
}

TEST(TlsMemory, OpenSslMemoryOfProfilesAndConnectionsIsReturnedWhenTheyClose)
{
    REQUIRE_TLS();
    ASSERT_TRUE(openSslMemory().counting);
    for (int i = 0; i < 2; ++i)
    {
        Profiles profiles;
        openDefaults(profiles);
        TlsPump pump(4096);
        connect(pump, profiles);
        EXPECT_EQ(roundTrip(pump.client, pump.server, "warm", 64), "warm");
    }
    OpenSslMemory before = openSslMemory();
    {
        Profiles profiles;
        openDefaults(profiles);
        TlsPump pump(4096);
        connect(pump, profiles);
        EXPECT_EQ(roundTrip(pump.client, pump.server, "counted", 64), "counted");
        EXPECT_GT(openSslMemory().live_bytes, before.live_bytes);
    }
    OpenSslMemory after = openSslMemory();
    EXPECT_EQ(after.live_bytes, before.live_bytes);
    EXPECT_EQ(after.live_blocks, before.live_blocks);
}

TEST(TlsMemory, EachConnectedPairHoldsABoundedAmountOfOpenSslMemory)
{
    REQUIRE_TLS();
    ASSERT_TRUE(openSslMemory().counting);
    const size_t pairs = 4;
    const size_t bound_bytes = 128 * 1024;
    Profiles profiles;
    openDefaults(profiles);
    {
        TlsPump warm(4096);
        connect(warm, profiles);
    }
    OpenSslMemory before = openSslMemory();
    kiotty_test::resetOpenSslPeak();
    std::vector<std::unique_ptr<TlsPump>> pumps;
    for (size_t i = 0; i < pairs; ++i)
    {
        pumps.emplace_back(new TlsPump(4096));
        connect(*pumps.back(), profiles);
        EXPECT_EQ(roundTrip(pumps.back()->client, pumps.back()->server, "bounded", 64), "bounded");
    }
    OpenSslMemory connected = openSslMemory();
    size_t held = (connected.live_bytes - before.live_bytes) / pairs;
    size_t peak = (connected.peak_bytes - before.live_bytes) / pairs;
    RecordProperty("held_bytes_per_pair", static_cast<int>(held));
    RecordProperty("peak_bytes_per_pair", static_cast<int>(peak));
    EXPECT_LT(held, bound_bytes);
    EXPECT_LT(peak, bound_bytes);
    pumps.clear();
    EXPECT_EQ(openSslMemory().live_bytes, before.live_bytes);
}

TEST(TlsEngine, DecodeDrainsPlaintextLeftInSslAcrossCalls)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    std::string text = pattern(3 * 4096);
    pump.client.encode(spanOf(text));
    move(pump.client, pump.server);
    Stage<4096> plain;
    std::string received;
    for (int i = 0; i < 3; ++i)
    {
        DecodeResult result = pump.server.decode(plain);
        ASSERT_TRUE(result);
        EXPECT_EQ(result.value, 4096u) << i;
        received += take(plain);
    }
    DecodeResult rest = pump.server.decode(plain);
    ASSERT_TRUE(rest);
    EXPECT_EQ(rest.value, 0u);
    EXPECT_EQ(received, text);
}

TEST(TlsEngine, DecodeCompactsCipherReceive)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    pump.client.encode(spanOf(pattern(2000)));
    move(pump.client, pump.server);
    Stage<4096> plain;
    DecodeResult result = pump.server.decode(plain);
    ASSERT_TRUE(result);
    StageBase& cipher = pump.server.cipherReceive();
    EXPECT_EQ(cipher.consumer().readable(), 0u);
    EXPECT_EQ(cipher.provider().writable(), cipher.capacity());
}

TEST(TlsEngine, EncodeNeverLeavesCipherSendEmptyForNonEmptyPlain)
{
    REQUIRE_TLS();
    const uint32_t versions[] = { KIOTTY_TLS_V12, KIOTTY_TLS_V13 };
    const size_t capacities[] = { 4096, MemoryLimits::SEND_CAPACITY };
    for (uint32_t version : versions)
    {
        KiottyNetTlsServerConfig server_config = serverConfig();
        KiottyNetTlsClientConfig client_config = clientConfig();
        server_config.versions = version;
        client_config.versions = version;
        Profiles profiles;
        ASSERT_EQ(profiles.server.openServer(server_config), KIOTTY_SUCCESS);
        ASSERT_EQ(profiles.client.openClient(client_config), KIOTTY_SUCCESS);
        for (size_t capacity : capacities)
        {
            TlsPump pump(capacity);
            ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_SUCCESS);
            pump.handshake();
            ASSERT_TRUE(pump.done());
            Stage<16> plain;
            ASSERT_TRUE(pump.client.decode(plain));
            std::string text = pattern(capacity);
            pump.server.encode(spanOf(text));
            EXPECT_GT(pump.server.cipherSend().consumer().readable(), capacity) << version << " " << capacity;
            EXPECT_EQ(roundTrip(pump.server, pump.client, "", capacity), text);
        }
    }
}

TEST(TlsEngine, EncodeIntoAFullCipherSendAssertsInDebugAndWritesNothingInRelease)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump(4096);
    connect(pump, profiles);
    std::string text = pattern(4096);
    pump.client.encode(spanOf(text));
    size_t before = pump.client.cipherSend().consumer().readable();
    EXPECT_DEBUG_DEATH(pump.client.encode(spanOf(text)), "");
    EXPECT_EQ(pump.client.cipherSend().consumer().readable(), before);
}

TEST(TlsEngine, EncodeOrDecodeOnInactiveEngineIsRejected)
{
    REQUIRE_TLS();
    TlsEngine engine;
    engine.limit(4096, 4096);
    Stage<64> plain;
    DecodeResult result = engine.decode(plain);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.code, KIOTTY_INVALID_ARGUMENT);
    EXPECT_DEBUG_DEATH(engine.encode(spanOf(std::string("x"))), "");
    EXPECT_EQ(engine.cipherSend().consumer().readable(), 0u);
}
