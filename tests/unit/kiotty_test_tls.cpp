#include "support/kiotty_test_openssl_memory.h"
#include "support/kiotty_test_tls.h"
#include "data/infra/tls/kiotty_tls_pipe.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using kiotty::net::implementation::Stage;
using kiotty::net::implementation::StageBase;
using kiotty::net::implementation::MemoryLimits;
using kiotty::net::implementation::DecodeResult;
using kiotty::net::implementation::TlsCapacity;
using kiotty::net::implementation::TlsEngine;
using kiotty::net::implementation::TlsPipe;
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
        ASSERT_EQ(profiles.server.openServer(serverConfig()), KIOTTY_NET_CODE_SUCCESS);
        ASSERT_EQ(profiles.client.openClient(clientConfig()), KIOTTY_NET_CODE_SUCCESS);
    }

    void connect(TlsPump& pump, Profiles& profiles)
    {
        ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
        pump.handshake();
        ASSERT_TRUE(pump.done());
        Stage plain{ 16 };
        DecodeResult swallowed = pump.client.decode(plain);
        ASSERT_TRUE(swallowed);
        ASSERT_EQ(swallowed.value, 0u);
    }

    std::string roundTrip(TlsEngine& from, TlsEngine& to, const std::string& text, size_t plain_capacity)
    {
        from.encode(spanOf(text));
        std::string received;
        Stage plain{ kiotty::net::implementation::MemoryLimits::RECEIVE_CAPACITY };
        plain.limit(plain_capacity);
        while (from.stages().send.consumer().readable() > 0)
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
    EXPECT_EQ(profile.openServer(serverConfig()), KIOTTY_NET_CODE_SUCCESS);
    EXPECT_TRUE(profile.valid());
    EXPECT_TRUE(profile.settings().is_server);
}

TEST(TlsProfile, OpenServerWithMissingFileReturnsCertificateNotFound)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig("missing.pem", "server.key")), KIOTTY_NET_CODE_TLS_CERTIFICATE_NOT_FOUND);
    EXPECT_FALSE(profile.valid());
}

TEST(TlsProfile, OpenServerWithKeyThatDoesNotMatchCertReturnsCertificateInvalid)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig("server.pem", "client.key")), KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID);
    EXPECT_FALSE(profile.valid());
}

TEST(TlsProfile, OpenServerWithGarbageFileReturnsCertificateInvalid)
{
    REQUIRE_TLS();
    TlsProfile profile;
    EXPECT_EQ(profile.openServer(serverConfig("garbage.pem", "server.key")), KIOTTY_NET_CODE_TLS_CERTIFICATE_INVALID);
    EXPECT_FALSE(profile.valid());
}

TEST(TlsProfile, OpenClientWithDefaultsSucceedsAndHandshakeTimeoutIsTheDefault)
{
    REQUIRE_TLS();
    KiottyNetTlsClientConfiguration configuration = clientConfig();
    configuration.trust_store_path[0] = '\0';
    TlsProfile profile;
    EXPECT_EQ(profile.openClient(configuration), KIOTTY_NET_CODE_SUCCESS);
    EXPECT_FALSE(profile.settings().is_server);
    EXPECT_TRUE(profile.settings().has_hostname_verification);
    EXPECT_EQ(profile.settings().handshake_timeout_milliseconds, kiotty::net::implementation::TLS_HANDSHAKE_TIMEOUT_MILLISECONDS);
}

TEST(TlsProfile, StubBuildReturnsNotSupportedForBothOpens)
{
    if (kiotty::net::implementation::tlsSupported())
    {
        GTEST_SKIP() << "built with KIOTTY_NET_USE_TLS=ON";
    }
    TlsProfile server;
    TlsProfile client;
    EXPECT_EQ(server.openServer(serverConfig()), KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
    EXPECT_EQ(client.openClient(clientConfig()), KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
    EXPECT_FALSE(server.valid());
    EXPECT_FALSE(client.valid());
    TlsEngine engine;
    EXPECT_EQ(engine.open(server, kiotty_test::textOf(nullptr), TlsCapacity{ 4096, 4096 }), KIOTTY_NET_CODE_TLS_NOT_SUPPORTED);
    EXPECT_FALSE(engine.status().is_active);
}

TEST(TlsEngine, InactiveEngineReportsActiveFalseAndAllocatesNoStage)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsEngine engine;
    EXPECT_EQ(engine.open(profiles.server, kiotty_test::textOf(nullptr), TlsCapacity{ 0, 0 }), KIOTTY_NET_CODE_INVALID_ARGUMENT);
    EXPECT_EQ(engine.stages().receive.capacity(), 0u);
    EXPECT_EQ(engine.stages().send.capacity(), 0u);
    EXPECT_FALSE(engine.status().is_active);
}

TEST(TlsEngine, ServerFirstStepIsReceiveAndClientFirstStepIsSend)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(pump.server.handshake(), TlsStep::RECEIVE);
    EXPECT_EQ(pump.server.stages().send.consumer().readable(), 0u);
    EXPECT_EQ(pump.client.handshake(), TlsStep::SEND);
    EXPECT_GT(pump.client.stages().send.consumer().readable(), 0u);
}

TEST(TlsEngine, PumpingCipherBytesBetweenClientAndServerReachesDoneOnBoth)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    int rounds = pump.handshake();
    EXPECT_TRUE(pump.done());
    EXPECT_LE(rounds, 6);
}

TEST(TlsEngine, Tls12OnlyProfilesAlsoReachDone)
{
    REQUIRE_TLS();
    KiottyNetTlsServerConfiguration server_config = serverConfig();
    KiottyNetTlsClientConfiguration client_config = clientConfig();
    server_config.versions = KIOTTY_NET_TLS_VERSION_V12;
    client_config.versions = KIOTTY_NET_TLS_VERSION_V12;
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(server_config), KIOTTY_NET_CODE_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(client_config), KIOTTY_NET_CODE_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
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
    EXPECT_GT(pump.client.stages().send.consumer().readable(), text.size() + 2 * 5);
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
    size_t record = pump.client.stages().send.consumer().readable();
    Stage plain{ 4096 };
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
    Stage plain{ 4096 };
    std::string received;
    while (pump.client.stages().send.consumer().readable() > 0)
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
    StageBase& cipher = pump.client.stages().send;
    size_t length = cipher.consumer().readable();
    cipher.consumer().peek()[length - 1] ^= 0x01;
    move(pump.client, pump.server);
    Stage plain{ 4096 };
    DecodeResult result = pump.server.decode(plain);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.code, KIOTTY_NET_CODE_TLS_RECORD_ERROR);
    EXPECT_EQ(pump.server.status().failure.code, KIOTTY_NET_CODE_TLS_RECORD_ERROR);
    EXPECT_STRNE(pump.server.status().failure.message, "");
}

TEST(TlsEngine, ClientRejectsServerSignedByUntrustedCaWithVerifyFailed)
{
    REQUIRE_TLS();
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig()), KIOTTY_NET_CODE_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(clientConfig("other_ca.pem")), KIOTTY_NET_CODE_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.client_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.client.status().failure.code, KIOTTY_NET_CODE_TLS_VERIFY_FAILED);
    EXPECT_STRNE(pump.client.status().failure.message, "");
}

TEST(TlsEngine, ClientWithVerifyServerOffAcceptsUntrustedCa)
{
    REQUIRE_TLS();
    KiottyNetTlsClientConfiguration configuration = clientConfig("other_ca.pem");
    configuration.verification = KIOTTY_NET_TLS_VERIFICATION_NONE;
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig()), KIOTTY_NET_CODE_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(configuration), KIOTTY_NET_CODE_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    pump.handshake();
    EXPECT_TRUE(pump.done());
}

TEST(TlsEngine, HostnameMismatchReturnsHostnameMismatch)
{
    REQUIRE_TLS();
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig("server_wrong_host.pem", "server_wrong_host.key")), KIOTTY_NET_CODE_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(clientConfig()), KIOTTY_NET_CODE_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client, "localhost"), KIOTTY_NET_CODE_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.client_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.client.status().failure.code, KIOTTY_NET_CODE_TLS_HOSTNAME_MISMATCH);
}

TEST(TlsEngine, ExpiredServerCertReturnsVerifyFailed)
{
    REQUIRE_TLS();
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(serverConfig("server_expired.pem", "server_expired.key")), KIOTTY_NET_CODE_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(clientConfig()), KIOTTY_NET_CODE_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.client_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.client.status().failure.code, KIOTTY_NET_CODE_TLS_VERIFY_FAILED);
}

TEST(TlsEngine, ServerRequiringClientAuthFailsWithoutClientCertAndSucceedsWithIt)
{
    REQUIRE_TLS();
    KiottyNetTlsServerConfiguration server_config = serverConfig();
    server_config.client_authentication = KIOTTY_NET_CLIENT_AUTHENTICATION_REQUIRED;
    copyText(server_config.trust_store_path, certPath("ca.pem"));
    TlsProfile server;
    ASSERT_EQ(server.openServer(server_config), KIOTTY_NET_CODE_SUCCESS);

    TlsProfile anonymous;
    ASSERT_EQ(anonymous.openClient(clientConfig()), KIOTTY_NET_CODE_SUCCESS);
    TlsPump rejected;
    ASSERT_EQ(rejected.open(server, anonymous), KIOTTY_NET_CODE_SUCCESS);
    rejected.handshake();
    ASSERT_EQ(rejected.server_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(rejected.server.status().failure.code, KIOTTY_NET_CODE_TLS_VERIFY_FAILED);

    KiottyNetTlsClientConfiguration client_config = clientConfig();
    copyText(client_config.identity.certificate_file, certPath("client.pem"));
    copyText(client_config.identity.key_file, certPath("client.key"));
    TlsProfile identified;
    ASSERT_EQ(identified.openClient(client_config), KIOTTY_NET_CODE_SUCCESS);
    TlsPump accepted;
    ASSERT_EQ(accepted.open(server, identified), KIOTTY_NET_CODE_SUCCESS);
    accepted.handshake();
    EXPECT_TRUE(accepted.done());
}

TEST(TlsEngine, VersionMismatchReturnsHandshakeFailed)
{
    REQUIRE_TLS();
    KiottyNetTlsServerConfiguration server_config = serverConfig();
    KiottyNetTlsClientConfiguration client_config = clientConfig();
    server_config.versions = KIOTTY_NET_TLS_VERSION_V13;
    client_config.versions = KIOTTY_NET_TLS_VERSION_V12;
    Profiles profiles;
    ASSERT_EQ(profiles.server.openServer(server_config), KIOTTY_NET_CODE_SUCCESS);
    ASSERT_EQ(profiles.client.openClient(client_config), KIOTTY_NET_CODE_SUCCESS);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    pump.handshake();
    ASSERT_EQ(pump.server_steps.back(), TlsStep::FAILED);
    EXPECT_EQ(pump.server.status().failure.code, KIOTTY_NET_CODE_TLS_HANDSHAKE_FAILED);
}

TEST(TlsEngine, PlaintextBytesFromPeerReturnHandshakeFailed)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsEngine server;
    kiotty::net::implementation::reserveTlsStages(server.stages(), TlsCapacity{ 4096, 4096 });
    ASSERT_EQ(server.open(profiles.server, kiotty_test::textOf(nullptr), TlsCapacity{ 4096, 4096 }), KIOTTY_NET_CODE_SUCCESS);
    put(server.stages().receive, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_EQ(server.handshake(), TlsStep::FAILED);
    EXPECT_EQ(server.status().failure.code, KIOTTY_NET_CODE_TLS_HANDSHAKE_FAILED);
}

TEST(TlsEngine, Tls13ServerSendsNoSessionTicketSoItIsDoneRightAfterTheClientFinished)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
    EXPECT_EQ(pump.client.handshake(), TlsStep::SEND);
    move(pump.client, pump.server);
    EXPECT_EQ(pump.server.handshake(), TlsStep::SEND);
    move(pump.server, pump.client);
    EXPECT_EQ(pump.client.handshake(), TlsStep::SEND);
    move(pump.client, pump.server);
    EXPECT_EQ(pump.server.handshake(), TlsStep::DONE);
    EXPECT_EQ(pump.server.stages().send.consumer().readable(), 0u);
    EXPECT_EQ(pump.client.stages().receive.consumer().readable(), 0u);
    EXPECT_EQ(pump.client.handshake(), TlsStep::DONE);
}

TEST(TlsEngine, KeyUpdateFromPeerLeavesTheReplyInCipherSendAfterDecode)
{
    REQUIRE_TLS();
    Profiles profiles;
    openDefaults(profiles);
    TlsPump pump;
    connect(pump, profiles);
    TlsPipe(pump.client).requestKeyUpdate();
    ASSERT_GT(pump.client.stages().send.consumer().readable(), 0u);
    move(pump.client, pump.server);
    Stage plain{ 4096 };
    DecodeResult result = pump.server.decode(plain);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value, 0u);
    EXPECT_GT(pump.server.stages().send.consumer().readable(), 0u);
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
    TlsPipe(pump.client).sendCloseNotify();
    move(pump.client, pump.server);
    Stage plain{ 4096 };
    DecodeResult result = pump.server.decode(plain);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.code, KIOTTY_NET_CODE_PEER_CLOSED);
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
    EXPECT_FALSE(pump.server.status().is_active);
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
    Stage plain{ 4096 };
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
    Stage plain{ 4096 };
    DecodeResult result = pump.server.decode(plain);
    ASSERT_TRUE(result);
    StageBase& cipher = pump.server.stages().receive;
    EXPECT_EQ(cipher.consumer().readable(), 0u);
    EXPECT_EQ(cipher.provider().writable(), cipher.capacity());
}

TEST(TlsEngine, EncodeNeverLeavesCipherSendEmptyForNonEmptyPlain)
{
    REQUIRE_TLS();
    const uint32_t versions[] = { KIOTTY_NET_TLS_VERSION_V12, KIOTTY_NET_TLS_VERSION_V13 };
    const size_t capacities[] = { 4096, MemoryLimits::SEND_CAPACITY };
    for (uint32_t version : versions)
    {
        KiottyNetTlsServerConfiguration server_config = serverConfig();
        KiottyNetTlsClientConfiguration client_config = clientConfig();
        server_config.versions = version;
        client_config.versions = version;
        Profiles profiles;
        ASSERT_EQ(profiles.server.openServer(server_config), KIOTTY_NET_CODE_SUCCESS);
        ASSERT_EQ(profiles.client.openClient(client_config), KIOTTY_NET_CODE_SUCCESS);
        for (size_t capacity : capacities)
        {
            TlsPump pump(capacity);
            ASSERT_EQ(pump.open(profiles.server, profiles.client), KIOTTY_NET_CODE_SUCCESS);
            pump.handshake();
            ASSERT_TRUE(pump.done());
            Stage plain{ 16 };
            ASSERT_TRUE(pump.client.decode(plain));
            std::string text = pattern(capacity);
            pump.server.encode(spanOf(text));
            EXPECT_GT(pump.server.stages().send.consumer().readable(), capacity) << version << " " << capacity;
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
    size_t before = pump.client.stages().send.consumer().readable();
    EXPECT_DEBUG_DEATH(pump.client.encode(spanOf(text)), "");
    EXPECT_EQ(pump.client.stages().send.consumer().readable(), before);
}

TEST(TlsEngine, EncodeOrDecodeOnInactiveEngineIsRejected)
{
    REQUIRE_TLS();
    TlsEngine engine;
    Stage plain{ 64 };
    DecodeResult result = engine.decode(plain);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.code, KIOTTY_NET_CODE_INVALID_ARGUMENT);
    EXPECT_DEBUG_DEATH(engine.encode(spanOf(std::string("x"))), "");
    EXPECT_EQ(engine.stages().send.consumer().readable(), 0u);
}
