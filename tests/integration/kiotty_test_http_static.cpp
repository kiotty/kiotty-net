#include "kiotty_net_http.h"

#include "support/kiotty_test_peer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    namespace fs = std::filesystem;

    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;

    struct FileApp
    {
        std::mutex                 mutex;
        std::vector<KiottyNetCode> codes;
        std::string                report_path;
        std::string                missing_path;
        std::string                escaping_path;

        void add(KiottyNetCode code)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            codes.push_back(code);
        }

        std::vector<KiottyNetCode> codeList()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            return codes;
        }
    };

    struct Response
    {
        int         status;
        std::string head;
        std::string body;
    };

    std::string patternOf(size_t length)
    {
        std::string text(length, '\0');
        for (size_t i = 0; i < length; ++i) { text[i] = static_cast<char>(i * 7 % 251); }
        return text;
    }

    void writeFile(const fs::path& path, const std::string& content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string fieldOf(const std::string& head, const std::string& name)
    {
        const std::string key = "\r\n" + name + ": ";
        const size_t start = head.find(key);
        if (start == std::string::npos) { return std::string(); }
        const size_t value = start + key.size();
        return head.substr(value, head.find("\r\n", value) - value);
    }

    Response readResponse(kiotty_test::LoopbackPeer& peer, bool is_head_request = false)
    {
        Response response = { -1, std::string(), std::string() };
        while (response.head.size() < 4 || response.head.compare(response.head.size() - 4, 4, "\r\n\r\n") != 0)
        {
            char next = 0;
            if (!peer.recvExactly(&next, 1, WAIT_MS)) { return response; }
            response.head.push_back(next);
        }
        response.status = std::atoi(response.head.substr(9, 3).c_str());
        const size_t length = is_head_request ? 0 : std::strtoul(fieldOf(response.head, "Content-Length").c_str(), nullptr, 10);
        response.body.resize(length);
        if (length > 0 && !peer.recvExactly(&response.body[0], length, WAIT_MS)) { response.status = -1; }
        return response;
    }

    void onReport(KiottyNetAppContext app_context, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        FileApp& app = *static_cast<FileApp*>(app_context);
        app.add(KiottyNet_setHttpFile(response, KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION, app.escaping_path.c_str()));
        app.add(KiottyNet_setHttpFile(response, KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION, app.missing_path.c_str()));
        app.add(KiottyNet_setHttpFile(response, KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM, app.report_path.c_str()));
        app.add(KiottyNet_setHttpFile(response, KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION, app.report_path.c_str()));
    }

    void onHealth(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        const char text[] = "{\"ok\":true}";
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, KiottyNetSpan{ text, sizeof(text) - 1 });
    }

    class HttpStaticTest : public ::testing::Test
    {
    protected:
        HttpStaticTest()
            : context(nullptr)
            , server(nullptr)
            , port(0)
            , root()
            , memory()
            , app()
            , _address()
            , _thread()
        {
        }

        void SetUp() override
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root = fs::temp_directory_path() / ("kiotty_static_" + std::to_string(stamp));
            writeFile(root / "www" / "index.html", "<h1>hi</h1>");
            writeFile(root / "www" / "js" / "app.js", "let a = 1;");
            writeFile(root / "secret.txt", "secret");
            writeFile(root / "report.csv", "a,b\n1,2\n");
            const KiottyNetContextResult opened = KiottyNet_initializeContext();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, opened.code);
            context = opened.handle;
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeHttpLimits(&limits));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeServerConfiguration(&configuration));
            const KiottyNetHttpServerResult created = KiottyNet_createHttpServer(context, &configuration, &limits);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, created.code);
            server = created.handle;
            const KiottyNetHttpRoute health = KiottyNet_makeHttpRoute(&app, onHealth, nullptr);
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/health", health));
        }

        void TearDown() override
        {
            if (server != nullptr) { KiottyNet_closeHttpServer(server); }
            if (_thread.joinable()) { _thread.join(); }
            if (server != nullptr) { KiottyNet_destroyHttpServer(context, server); }
            KiottyNet_releaseContext(context);
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }

        std::string pathOf(const fs::path& path) const
        {
            return path.string();
        }

        void start()
        {
            port = kiotty_test::unusedPort();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, port));
            _thread = std::thread([this]() { KiottyNet_runHttpServer(server, &_address, nullptr); });
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_awaitHttpServer(server, WAIT_MS));
        }

        Response ask(kiotty_test::LoopbackPeer& peer, const std::string& request, bool is_head_request = false)
        {
            EXPECT_TRUE(peer.sendAll(request.data(), request.size()));
            return readResponse(peer, is_head_request);
        }

        KiottyNetContext             context;
        KiottyNetHttpServerHandle    server;
        uint16_t                     port;
        fs::path                     root;
        std::string                  memory;
        FileApp                      app;
        KiottyNetHttpLimits          limits;
        KiottyNetServerConfiguration configuration;

    private:
        KiottyNetAddress _address;
        std::thread      _thread;
    };
}

TEST_F(HttpStaticTest, DirectoryServesFilesWithTheContentTypeOfTheirExtension)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpDirectory(server, "/static/", pathOf(root / "www").c_str()));
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    const Response page = ask(peer, "GET /static/index.html HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, page.status);
    EXPECT_EQ("<h1>hi</h1>", page.body);
    EXPECT_EQ("text/html; charset=utf-8", fieldOf(page.head, "Content-Type"));
    const Response script = ask(peer, "GET /static/js/app.js HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, script.status);
    EXPECT_EQ("let a = 1;", script.body);
    EXPECT_EQ("text/javascript; charset=utf-8", fieldOf(script.head, "Content-Type"));
    const Response head = ask(peer, "HEAD /static/index.html HTTP/1.1\r\n\r\n", true);
    EXPECT_EQ(200, head.status);
    EXPECT_EQ("11", fieldOf(head.head, "Content-Length"));
    EXPECT_EQ(200, ask(peer, "GET /health HTTP/1.1\r\n\r\n").status);
}

TEST_F(HttpStaticTest, MissingFileOrADirectoryIsNotFound)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpDirectory(server, "/static/", pathOf(root / "www").c_str()));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpFile(server, "/gone.txt", pathOf(root / "gone.txt").c_str()));
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    EXPECT_EQ(404, ask(peer, "GET /static/none.txt HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(404, ask(peer, "GET /static/js HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(404, ask(peer, "GET /static/js/ HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(404, ask(peer, "GET /gone.txt HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(200, ask(peer, "GET /health HTTP/1.1\r\n\r\n").status);
}

TEST_F(HttpStaticTest, PathsThatLeaveTheDirectoryAreForbidden)
{
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpDirectory(server, "/static/", pathOf(root / "www").c_str()));
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    EXPECT_EQ(403, ask(peer, "GET /static/../secret.txt HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(403, ask(peer, "GET /static/js/../../secret.txt HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(403, ask(peer, "GET /static/..\\secret.txt HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(403, ask(peer, "GET /static/c:secret.txt HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(404, ask(peer, "GET /static/index..html HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(200, ask(peer, "GET /static/js/app.js HTTP/1.1\r\n\r\n").status);
}

TEST_F(HttpStaticTest, MemoryServesBinaryAndOnlyGetAndHead)
{
    memory = std::string("\0asm\x01\0\0\0", 8) + patternOf(100);
    const std::string& wasm = memory;
    const KiottyNetSpan data = { wasm.data(), wasm.size() };
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpMemory(server, "/app.wasm", KIOTTY_NET_HTTP_CONTENT_TYPE_WASM, data));
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    const Response got = ask(peer, "GET /app.wasm HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, got.status);
    EXPECT_EQ(wasm, got.body);
    EXPECT_EQ("application/wasm", fieldOf(got.head, "Content-Type"));
    const Response head = ask(peer, "HEAD /app.wasm HTTP/1.1\r\n\r\n", true);
    EXPECT_EQ(std::to_string(wasm.size()), fieldOf(head.head, "Content-Length"));
    const Response post = ask(peer, "POST /app.wasm HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    EXPECT_EQ(405, post.status);
    EXPECT_EQ("GET, HEAD, OPTIONS", fieldOf(post.head, "Allow"));
}

TEST_F(HttpStaticTest, LargeFileGoesOutInPiecesAndTheConnectionStaysOpen)
{
    const std::string content = patternOf(configuration.send_capacity * 5 + 3);
    writeFile(root / "big.bin", content);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpFile(server, "/big.bin", pathOf(root / "big.bin").c_str()));
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    const Response got = ask(peer, "GET /big.bin HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, got.status);
    EXPECT_EQ("application/octet-stream", fieldOf(got.head, "Content-Type"));
    EXPECT_TRUE(content == got.body);
    EXPECT_EQ(200, ask(peer, "GET /health HTTP/1.1\r\n\r\n").status);
}

TEST_F(HttpStaticTest, RouteAnswersWithAFile)
{
    app.report_path = pathOf(root / "report.csv");
    app.missing_path = pathOf(root / "none.csv");
    app.escaping_path = pathOf(root / "www" / ".." / "report.csv");
    const KiottyNetHttpRoute report = KiottyNet_makeHttpRoute(&app, onReport, nullptr);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_addHttpRoute(server, KIOTTY_NET_HTTP_METHOD_GET, "/report", report));
    start();
    kiotty_test::LoopbackPeer peer;
    ASSERT_TRUE(peer.connectTcp(port));
    const Response got = ask(peer, "GET /report HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, got.status);
    EXPECT_EQ("a,b\n1,2\n", got.body);
    EXPECT_EQ("text/csv; charset=utf-8", fieldOf(got.head, "Content-Type"));
    const std::vector<KiottyNetCode> expected = {
        KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_NOT_FOUND,
        KIOTTY_NET_CODE_INVALID_ARGUMENT, KIOTTY_NET_CODE_SUCCESS,
    };
    EXPECT_EQ(expected, app.codeList());
}

TEST_F(HttpStaticTest, RegistrationChecksPathsTypesAndTiming)
{
    const char broken[] = { '\xC3', '\x28' };
    const KiottyNetSpan text = { "x", 1 };
    const KiottyNetSpan bad_text = { broken, sizeof(broken) };
    const std::string www = pathOf(root / "www");
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_serveHttpDirectory(server, "/static", www.c_str()));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_serveHttpDirectory(server, "/static/", ""));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_serveHttpFile(server, "/a.txt", nullptr));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_serveHttpMemory(server, "/m", KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION, text));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_serveHttpMemory(server, "/m", KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM, text));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, KiottyNet_serveHttpMemory(server, "/m", KIOTTY_NET_HTTP_CONTENT_TYPE_CUSTOM, text));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ENCODING, KiottyNet_serveHttpMemory(server, "/m", KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN, bad_text));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpMemory(server, "/m", KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM, bad_text));
    EXPECT_EQ(KIOTTY_NET_CODE_DUPLICATE_RULE, KiottyNet_serveHttpFile(server, "/m", www.c_str()));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_serveHttpDirectory(server, "/static/", www.c_str()));
    start();
    EXPECT_EQ(KIOTTY_NET_CODE_BUSY, KiottyNet_serveHttpFile(server, "/late.txt", www.c_str()));
}

#endif
