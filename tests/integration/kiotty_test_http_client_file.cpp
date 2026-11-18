#include "kiotty_net_http.h"

#include "support/kiotty_test_http_site.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    namespace fs = std::filesystem;

    const uint32_t WAIT_MS = 5000;
    const size_t   SITE_BODY_BYTES = 256 * 1024;
    const size_t   FILE_BYTES = 100000;
    const size_t   REFUSED_FILE_BYTES = 4 * 1024 * 1024;
    const size_t   DRAIN_PIECE_BYTES = 64 * 1024;
    const uint32_t ARRIVAL_MS = 100;

    using kiotty_test::readBody;

    std::string patternText(size_t length)
    {
        std::string text(length, 'a');
        for (size_t i = 0; i < text.size(); ++i) { text[i] = static_cast<char>('a' + i % 26); }
        return text;
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

    bool drain(kiotty_test::LoopbackPeer& peer, size_t length)
    {
        std::string piece(DRAIN_PIECE_BYTES, '\0');
        size_t      done = 0;
        while (done < length)
        {
            const size_t want = std::min(piece.size(), length - done);
            if (!peer.recvExactly(&piece[0], want, WAIT_MS)) { return false; }
            done += want;
        }
        return true;
    }

    class HttpClientFileTest : public ::testing::Test
    {
    protected:
        HttpClientFileTest()
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
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            directory = fs::temp_directory_path() / ("kiotty_upload_" + std::to_string(stamp));
            fs::create_directories(directory);
        }

        void TearDown() override
        {
            if (client != nullptr) { KiottyNet_destroyHttpClient(context, client); }
            site.reset();
            KiottyNet_releaseContext(context);
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }

        std::string makeFile(const std::string& name, const std::string& content)
        {
            const fs::path path = directory / name;
            std::ofstream out(path, std::ios::binary);
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            return path.string();
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

        KiottyNetHttpReply sendFile(const std::string& path)
        {
            KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_PUT, "/echo");
            request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM;
            return KiottyNet_sendHttpFile(client, &request, nullptr, 0, path.c_str(), WAIT_MS);
        }

        std::future<KiottyNetHttpReply> sendFileLater(const std::string& path)
        {
            return std::async(std::launch::async, [this, path]() { return sendFile(path); });
        }

        KiottyNetContext                       context;
        KiottyNetHttpClientHandle              client;
        std::unique_ptr<kiotty_test::HttpSite> site;
        KiottyNetHttpLimits                    limits;
        KiottyNetClientConfiguration           client_configuration;
        fs::path                               directory;
    };
}

TEST_F(HttpClientFileTest, FileIsSentWithItsLengthAndArrivesWhole)
{
    connectToSite();
    const std::string content = patternText(FILE_BYTES);
    const KiottyNetHttpReply reply = sendFile(makeFile("whole.bin", content));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(200, reply.status);
    EXPECT_EQ("PUT:" + content, readBody(client, WAIT_MS).text);
    const KiottyNetHttpReply again = sendFile(makeFile("small.txt", "abc"));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, again.code);
    EXPECT_EQ("PUT:abc", readBody(client, WAIT_MS).text);
    EXPECT_EQ(1u, site->connections());
}

TEST_F(HttpClientFileTest, EmptyFileIsSentWithLengthZero)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::future<KiottyNetHttpReply> sent = sendFileLater(makeFile("empty.bin", ""));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    EXPECT_NE(std::string::npos, received.find("Content-Length: 0\r\n"));
    EXPECT_EQ(std::string::npos, received.find("Transfer-Encoding"));
    const std::string reply = "HTTP/1.1 204 No Content\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const KiottyNetHttpReply answered = sent.get();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(204, answered.status);
}

TEST_F(HttpClientFileTest, MissingFileIsNotFoundAndSendsNothing)
{
    connectToSite();
    EXPECT_EQ(KIOTTY_NET_CODE_NOT_FOUND, sendFile((directory / "missing.bin").string()).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, sendFile("").code);
    const KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_PUT, "/echo");
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_sendHttpFile(client, &request, nullptr, 0, nullptr, WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_sendHttpFile(client, nullptr, nullptr, 0, "x", WAIT_MS).code);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_sendHttpFile(nullptr, &request, nullptr, 0, "x", WAIT_MS).code);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
}

TEST_F(HttpClientFileTest, FileThatTheServerRefusesEarlyGivesItsReply)
{
    connectToSite();
    const KiottyNetHttpReply reply = sendFile(makeFile("large.bin", patternText(REFUSED_FILE_BYTES)));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, reply.code);
    EXPECT_EQ(413, reply.status);
    readBody(client, WAIT_MS);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_getHttp(client, "/echo", nullptr, 0, WAIT_MS).code);
    EXPECT_EQ("GET:", readBody(client, WAIT_MS).text);
    EXPECT_EQ(0u, site->count(KIOTTY_NET_HTTP_METHOD_PUT));
}

TEST_F(HttpClientFileTest, InterimReplyDuringAFileIsSkipped)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::future<KiottyNetHttpReply> sent = sendFileLater(makeFile("whole.bin", patternText(FILE_BYTES)));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    const std::string interim = "HTTP/1.1 100 Continue\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(interim.data(), interim.size()));
    ASSERT_TRUE(drain(peer, FILE_BYTES));
    const std::string reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    const KiottyNetHttpReply answered = sent.get();
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(201, answered.status);
}

TEST_F(HttpClientFileTest, FinalReplyDuringAFileIsReturned)
{
    kiotty_test::LoopbackPeer peer;
    connectToPeer(peer);
    std::future<KiottyNetHttpReply> sent = sendFileLater(makeFile("large.bin", patternText(REFUSED_FILE_BYTES)));
    std::string received;
    ASSERT_TRUE(readUntil(peer, received, "\r\n\r\n"));
    const std::string reply = "HTTP/1.1 507 Insufficient Storage\r\nContent-Length: 4\r\n\r\nfull";
    ASSERT_TRUE(peer.sendAll(reply.data(), reply.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(ARRIVAL_MS));
    std::future<bool> drained = std::async(std::launch::async, [&peer]() { return drain(peer, REFUSED_FILE_BYTES); });
    const KiottyNetHttpReply answered = sent.get();
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, answered.code);
    EXPECT_EQ(507, answered.status);
    EXPECT_EQ("full", readBody(client, WAIT_MS).text);
    KiottyNet_disconnectHttpClient(client);
    drained.wait();
}

#endif
