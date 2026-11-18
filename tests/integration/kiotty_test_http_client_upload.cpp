#include "kiotty_net_http.h"

#include "support/kiotty_test_http_site.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const uint32_t WAIT_MS = 5000;
    const size_t   SITE_BODY_BYTES = 256 * 1024;
    const size_t   LARGE_BODY_BYTES = 100000;
    const size_t   PIECE_BYTES = 7000;
    const uint32_t KEEP_ALIVE_MS = 150;
    const uint32_t PAST_KEEP_ALIVE_MS = 800;
    const uint32_t ARRIVAL_MS = 100;
    const uint32_t LONG_CALL_MS = 20000;
    const uint32_t WAKE_BOUND_MS = 2000;
    const uint32_t CLOSE_DELAY_MS = 200;
    const size_t   STUCK_PIECE_BYTES = 1024 * 1024;
    const size_t   STUCK_ROUNDS = 1024;
    const size_t   REFUSED_BODY_BYTES = 4 * 1024 * 1024;
    const size_t   REFUSED_PIECE_BYTES = 64 * 1024;

    using kiotty_test::readBody;
    using kiotty_test::textSpan;

    std::string patternText(size_t length)
    {
        std::string text(length, 'a');
        for (size_t i = 0; i < text.size(); ++i) { text[i] = static_cast<char>('a' + i % 26); }
        return text;
    }

    KiottyNetHttpClientRequest uploadOf(KiottyNetHttpMethod method, const char* path, KiottyNetHttpContentType type)
    {
        KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(method, path);
        request.type = type;
        return request;
    }

    KiottyNetAddress tcpAddress(uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&address, "127.0.0.1", port));
        return address;
    }

    bool readUntil(kiotty_test::LoopbackPeer& peer, std::string& received, const std::string& end)
    {
        received.clear();
        while (received.size() < end.size() || received.compare(received.size() - end.size(), end.size(), end) != 0)
        {
            char next = 0;
            if (!peer.recvExactly(&next, 1, WAIT_MS)) { return false; }
            received.push_back(next);
        }
        return true;
    }

    bool isConnectionLoss(KiottyNetCode code)
    {
        return code == KIOTTY_NET_CODE_PEER_CLOSED || code == KIOTTY_NET_CODE_SOCKET_ERROR;
    }

    class HttpClientUploadTest : public ::testing::Test
    {
    protected:
        HttpClientUploadTest()
            : context(nullptr)
            , client(nullptr)
            , site()
        {
        }

        void SetUp() override
        {
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            site.reset(new kiotty_test::HttpSite(context));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpBodyCapacity(&limits, SITE_BODY_BYTES));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpResponseCapacity(&limits, SITE_BODY_BYTES));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeClientConfiguration(&client_configuration));
        }

        void TearDown() override
        {
            if (client != nullptr) { KiottyNet_destroyHttpClient(context, client); }
            site.reset();
            KiottyNet_releaseContext(context);
        }

        void connectToSite()
        {
            ASSERT_TRUE(site->start(limits, nullptr));
            createClient();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &site->address(), nullptr));
        }

        void connectToPeer(kiotty_test::LoopbackPeer& peer)
        {
            ASSERT_TRUE(peer.listenTcp(0));
            createClient();
            const KiottyNetAddress address = tcpAddress(peer.port());
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_connectHttpClient(client, &address, nullptr));
            ASSERT_TRUE(peer.accept(WAIT_MS));
        }

        void createClient()
        {
            const KiottyNetHttpClientResult created = KiottyNet_createHttpClient(context, &client_configuration, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            client = created.handle;
        }

        KiottyNetCode begin(KiottyNetHttpMethod method, uint64_t body_length)
        {
            const KiottyNetHttpClientRequest request =
                uploadOf(method, "/echo", KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM);
            return KiottyNet_beginHttpRequest(client, &request, nullptr, 0, body_length, WAIT_MS);
        }

        KiottyNetResult write(const std::string& piece)
        {
            return KiottyNet_writeHttpRequestBody(client, piece.data(), piece.size(), WAIT_MS);
        }

        void writeInPieces(const std::string& body)
        {
            for (size_t offset = 0; offset < body.size(); offset += PIECE_BYTES)
            {
                const std::string piece = body.substr(offset, PIECE_BYTES);
                const KiottyNetResult sent = write(piece);
                ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, sent.code);
                ASSERT_EQ(static_cast<int32_t>(piece.size()), sent.bytes);
            }
        }

        std::string finishAndRead()
        {
            const KiottyNetHttpReply reply = KiottyNet_finishHttpRequest(client, WAIT_MS);
            EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
            EXPECT_EQ(200, reply.status);
            return readBody(client, WAIT_MS).text;
        }

        KiottyNetContext                       context;
        KiottyNetHttpClientHandle              client;
        std::unique_ptr<kiotty_test::HttpSite> site;
        KiottyNetHttpLimits                    limits;
        KiottyNetClientConfiguration           client_configuration;
    };
}

TEST_F(HttpClientUploadTest, KnownLengthBodyWrittenInPiecesArrivesWhole)
{
    connectToSite();
    const std::string body = patternText(LARGE_BODY_BYTES);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, body.size()));
    writeInPieces(body);
    EXPECT_EQ("PUT:" + body, finishAndRead());
}

TEST_F(HttpClientUploadTest, UnknownLengthBodyIsChunkedAndArrivesWhole)
{
    connectToSite();
    const std::string body = patternText(LARGE_BODY_BYTES);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_POST, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN));
    writeInPieces(body);
    EXPECT_EQ("POST:" + body, finishAndRead());
}

TEST_F(HttpClientUploadTest, ZeroLengthBodiesFinishAtOnceAndEmptyWritesSendNothing)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 0));
    EXPECT_EQ("PUT:", finishAndRead());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_POST, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN));
    const KiottyNetResult empty = KiottyNet_writeHttpRequestBody(client, nullptr, 0, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, empty.code);
    EXPECT_EQ(0, empty.bytes);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("ab").code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("").code);
    EXPECT_EQ("POST:ab", finishAndRead());
    EXPECT_EQ(1u, site->connections());
}

TEST_F(HttpClientUploadTest, WriteOverTheDeclaredLengthIsRefusedAndTheUploadGoesOn)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 5));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    const KiottyNetResult over = write("def");
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, over.code);
    EXPECT_EQ(0, over.bytes);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("de").code);
    EXPECT_EQ("PUT:abcde", finishAndRead());
    EXPECT_EQ(1u, site->connections());
}

TEST_F(HttpClientUploadTest, FinishShortOfTheDeclaredLengthIsInvalidAndClosesTheConnection)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 10));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write("defg").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
}

TEST_F(HttpClientUploadTest, WriteOrFinishWithoutBeginIsInvalidArgument)
{
    connectToSite();
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write("abc").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 1));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("x").code);
    EXPECT_EQ("PUT:x", finishAndRead());
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write("y").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
}

TEST_F(HttpClientUploadTest, BeginChecksItsArgumentsAndKeepsAnUploadItRefuses)
{
    connectToSite();
    KiottyNetHttpClientRequest with_body = uploadOf(KIOTTY_NET_HTTP_METHOD_PUT, "/echo", KIOTTY_NET_HTTP_CONTENT_TYPE_JSON);
    with_body.body = textSpan("{}");
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_beginHttpRequest(client, &with_body, nullptr, 0, 2, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_beginHttpRequest(client, nullptr, nullptr, 0, 2, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_beginHttpRequest(nullptr, &with_body, nullptr, 0, 2, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_writeHttpRequestBody(nullptr, "a", 1, WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(nullptr, WAIT_MS).code);

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 4));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("ab").code);
    const KiottyNetHttpHeader chunked = KiottyNet_makeHttpHeader("Transfer-Encoding", "chunked");
    const KiottyNetHttpClientRequest plain = uploadOf(KIOTTY_NET_HTTP_METHOD_POST, "/echo", KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_beginHttpRequest(client, &plain, &chunked, 1, 4, WAIT_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_writeHttpRequestBody(client, nullptr, 1, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("cd").code);
    EXPECT_EQ("PUT:abcd", finishAndRead());
}

TEST_F(HttpClientUploadTest, BeginWithoutConnectIsInvalidArgument)
{
    createClient();
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, begin(KIOTTY_NET_HTTP_METHOD_PUT, 1));
}

TEST_F(HttpClientUploadTest, ReadBodyWhileUploadingIsInvalidArgument)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    char buffer[16];
    const KiottyNetResult read = KiottyNet_readHttpReplyBody(client, buffer, sizeof(buffer), WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, read.code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    EXPECT_EQ("PUT:abc", finishAndRead());
}

TEST_F(HttpClientUploadTest, AnotherRequestDuringAnUploadDropsItWithItsConnection)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 10));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write("defg").code);

    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 10));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_POST, 2));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("xy").code);
    EXPECT_EQ("POST:xy", finishAndRead());
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
    EXPECT_EQ(2u, site->connections());
}

TEST_F(HttpClientUploadTest, UnreadReplyBodyIsDroppedByBegin)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("def").code);
    EXPECT_EQ("PUT:def", finishAndRead());
    EXPECT_EQ(2u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
}

TEST_F(HttpClientUploadTest, DisconnectDuringAnUploadEndsIt)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    KiottyNet_disconnectHttpClient(client);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write("abc").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    EXPECT_EQ("PUT:abc", finishAndRead());
}

TEST_F(HttpClientUploadTest, ChunkedWireBytesAreOneChunkPerWriteAndALastChunk)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    const KiottyNetHttpClientRequest request =
        uploadOf(KIOTTY_NET_HTTP_METHOD_POST, "/v1/samples", KIOTTY_NET_HTTP_CONTENT_TYPE_NDJSON);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS,
              KiottyNet_beginHttpRequest(client, &request, nullptr, 0, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN, WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("{\"t\":1}\n").code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write(patternText(26)).code);
    std::future<KiottyNetHttpReply> finished =
        std::async(std::launch::async, [this]() { return KiottyNet_finishHttpRequest(client, WAIT_MS); });
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n0\r\n\r\n"));
    const std::string reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const KiottyNetHttpReply answered = finished.get();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(201, answered.status);
    EXPECT_EQ("POST /v1/samples HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(peer.port()) + "\r\n"
              "Content-Type: application/x-ndjson\r\nTransfer-Encoding: chunked\r\n\r\n"
              "8\r\n{\"t\":1}\n\r\n"
              "1a\r\nabcdefghijklmnopqrstuvwxyz\r\n"
              "0\r\n\r\n",
              received);
}

TEST_F(HttpClientUploadTest, KnownLengthWireBytesAreTheHeadThenTheRawPieces)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 6));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("def").code);
    std::future<KiottyNetHttpReply> finished =
        std::async(std::launch::async, [this]() { return KiottyNet_finishHttpRequest(client, WAIT_MS); });
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "abcdef"));
    const std::string reply = "HTTP/1.1 204 No Content\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const KiottyNetHttpReply answered = finished.get();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(204, answered.status);
    EXPECT_EQ("PUT /echo HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(peer.port()) + "\r\n"
              "Content-Type: application/octet-stream\r\nContent-Length: 6\r\n\r\nabcdef",
              received);
}

TEST_F(HttpClientUploadTest, BeginOnAnIdleConnectionTheServerClosedOpensANewOne)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_configureHttpTimeouts(&limits, WAIT_MS, KEEP_ALIVE_MS));
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    std::this_thread::sleep_for(std::chrono::milliseconds(PAST_KEEP_ALIVE_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_POST, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    EXPECT_EQ("POST:abc", finishAndRead());
    EXPECT_EQ(2u, site->connections());
    EXPECT_EQ(1u, site->count(KIOTTY_NET_HTTP_METHOD_POST));
}

TEST_F(HttpClientUploadTest, BeginOnAnIdleConnectionTheServerResetOpensANewOne)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_TRUE(peer.reset());
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    ASSERT_TRUE(peer.accept(WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\nabc"));
    EXPECT_EQ(0u, received.find("PUT /echo HTTP/1.1\r\n"));
}

TEST_F(HttpClientUploadTest, BeginDoesNotReuseAConnectionWithUnaskedBytes)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    const std::string unasked = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(unasked.data(), unasked.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 3));
    ASSERT_TRUE(peer.reset());
    ASSERT_TRUE(peer.accept(WAIT_MS));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    std::future<KiottyNetHttpReply> finished =
        std::async(std::launch::async, [this]() { return KiottyNet_finishHttpRequest(client, WAIT_MS); });
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\nabc"));
    const std::string reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const KiottyNetHttpReply answered = finished.get();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(201, answered.status);
}

TEST_F(HttpClientUploadTest, CloseWakesAWriteBlockedOnAPeerThatDoesNotRead)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_POST, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN));
    const std::vector<char> stuck(STUCK_PIECE_BYTES, 'x');
    std::thread closer([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(CLOSE_DELAY_MS));
        KiottyNet_closeHttpClient(client);
    });
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    KiottyNetResult woken = { 0, KIOTTY_NET_CODE_SUCCESS };
    for (size_t round = 0; round < STUCK_ROUNDS && woken.code == KIOTTY_NET_CODE_SUCCESS; ++round)
    {
        woken = KiottyNet_writeHttpRequestBody(client, stuck.data(), stuck.size(), LONG_CALL_MS);
    }
    const uint32_t elapsed = kiotty_test::elapsedMilliseconds(started);
    closer.join();
    EXPECT_EQ(KIOTTY_NET_CODE_CLOSED, woken.code);
    EXPECT_EQ(0, woken.bytes);
    EXPECT_LT(elapsed, CLOSE_DELAY_MS + WAKE_BOUND_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write("abc").code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
}

TEST_F(HttpClientUploadTest, FinishShowsTheReplyOfAServerThatRefusedTheBodyEarly)
{
    connectToSite();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, REFUSED_BODY_BYTES));
    const std::string piece = patternText(REFUSED_PIECE_BYTES);
    for (size_t offset = 0; offset < REFUSED_BODY_BYTES; offset += piece.size())
    {
        if (write(piece).code != KIOTTY_NET_CODE_SUCCESS) { break; }
    }
    const KiottyNetHttpReply reply = KiottyNet_finishHttpRequest(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(413, reply.status);
    readBody(client, WAIT_MS);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write(piece).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
}

TEST_F(HttpClientUploadTest, FinishAfterAWriteThePeerCutOffShowsTheReplyItSentFirst)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, REFUSED_BODY_BYTES));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    const std::string reply = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 4\r\n\r\nnope";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    ASSERT_TRUE(peer.shutdownWrite());
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    peer.close();
    const std::string piece = patternText(REFUSED_PIECE_BYTES);
    KiottyNetCode stopped = KIOTTY_NET_CODE_SUCCESS;
    for (size_t offset = 0; offset < REFUSED_BODY_BYTES && stopped == KIOTTY_NET_CODE_SUCCESS; offset += piece.size())
    {
        stopped = write(piece).code;
    }
    EXPECT_EQ(KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING, stopped);
    const KiottyNetHttpReply answered = KiottyNet_finishHttpRequest(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(401, answered.status);
    EXPECT_EQ("nope", readBody(client, WAIT_MS).text);
}

TEST_F(HttpClientUploadTest, InterimReplyWhileUploadingIsSkippedAndTheBodyGoesOn)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 6));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    const std::string interim = "HTTP/1.1 100 Continue\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(interim.data(), interim.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, write("abc").code);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, write("def").code);
    std::future<KiottyNetHttpReply> finished =
        std::async(std::launch::async, [this]() { return KiottyNet_finishHttpRequest(client, WAIT_MS); });
    ASSERT_TRUE(readUntil(peer, received, "abcdef"));
    const std::string reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const KiottyNetHttpReply answered = finished.get();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(201, answered.status);
}

TEST_F(HttpClientUploadTest, FinalReplyWhileUploadingStopsTheWriteAndFinishGivesIt)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, REFUSED_BODY_BYTES));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    const std::string reply = "HTTP/1.1 507 Insufficient Storage\r\nContent-Length: 4\r\n\r\nfull";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const std::string piece = patternText(REFUSED_PIECE_BYTES);
    KiottyNetResult stopped = { 0, KIOTTY_NET_CODE_SUCCESS };
    for (size_t offset = 0; offset < REFUSED_BODY_BYTES && stopped.code == KIOTTY_NET_CODE_SUCCESS; offset += piece.size())
    {
        stopped = KiottyNet_writeHttpRequestBody(client, piece.data(), piece.size(), WAIT_MS);
    }
    EXPECT_EQ(KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING, stopped.code);
    EXPECT_EQ(0, stopped.bytes);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, write(piece).code);
    const KiottyNetHttpReply answered = KiottyNet_finishHttpRequest(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(507, answered.status);
    EXPECT_EQ("full", readBody(client, WAIT_MS).text);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
    ASSERT_TRUE(peer.reset());
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 1));
    ASSERT_TRUE(peer.accept(WAIT_MS));
}

TEST_F(HttpClientUploadTest, DisconnectAfterAnEarlyReplyEndsTheUpload)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, 10));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    const std::string reply = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    EXPECT_EQ(KIOTTY_NET_CODE_DATA_ARRIVED_WHILE_WRITING, write("abc").code);
    KiottyNet_disconnectHttpClient(client);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
}

TEST_F(HttpClientUploadTest, FinishAfterAWriteThePeerCutOffWithoutAReplyGivesTheLoss)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, begin(KIOTTY_NET_HTTP_METHOD_PUT, REFUSED_BODY_BYTES));
    ASSERT_TRUE(peer.reset());
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    const std::string piece = patternText(REFUSED_PIECE_BYTES);
    KiottyNetCode stopped = KIOTTY_NET_CODE_SUCCESS;
    for (size_t offset = 0; offset < REFUSED_BODY_BYTES && stopped == KIOTTY_NET_CODE_SUCCESS; offset += piece.size())
    {
        stopped = write(piece).code;
    }
    EXPECT_TRUE(isConnectionLoss(stopped)) << stopped;
    const KiottyNetCode finished = KiottyNet_finishHttpRequest(client, WAIT_MS).code;
    EXPECT_TRUE(isConnectionLoss(finished)) << finished;
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_finishHttpRequest(client, WAIT_MS).code);
}

#endif
