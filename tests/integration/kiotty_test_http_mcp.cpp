#include "kiotty_net_http.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    namespace fs = std::filesystem;

    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;
    const uint32_t PING_MS = 100;
    const char*    BOTH_ACCEPT = "Accept: application/json, text/event-stream\r\n";

    std::string textOf(KiottyNetSpan span)
    {
        return span.data == nullptr ? std::string() : std::string(span.data, span.length);
    }

    KiottyNetSpan spanOf(const std::string& text)
    {
        return KiottyNetSpan{ text.data(), text.size() };
    }

    bool contains(KiottyNetSpan span, const char* needle)
    {
        return textOf(span).find(needle) != std::string::npos;
    }

    struct McpSession
    {
        std::deque<std::string> events;
        bool                    is_closed = false;
        bool                    is_streaming = false;
    };

    struct NextEvent
    {
        std::string event;
        bool        has_event;
        bool        is_closed;
    };

    class McpApp
    {
    public:
        std::string create()
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            const std::string id = "s-" + std::to_string(++_next_id);
            _sessions[id] = McpSession();
            return id;
        }

        bool has(const std::string& id)
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            return _sessions.count(id) == 1;
        }

        bool attachStream(const std::string& id)
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            auto found = _sessions.find(id);
            if (found == _sessions.end() || found->second.is_streaming) { return false; }
            found->second.is_streaming = true;
            return true;
        }

        void push(const std::string& id, const std::string& event)
        {
            {
                const std::lock_guard<std::mutex> lock(_mutex);
                _sessions[id].events.push_back(event);
            }
            _changed.notify_all();
        }

        void remove(const std::string& id)
        {
            {
                const std::lock_guard<std::mutex> lock(_mutex);
                auto found = _sessions.find(id);
                if (found != _sessions.end()) { found->second.is_closed = true; }
            }
            _changed.notify_all();
        }

        NextEvent waitFor(const std::string& id, uint32_t timeout_milliseconds)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            McpSession& session = _sessions[id];
            _changed.wait_for(lock, std::chrono::milliseconds(timeout_milliseconds), [&session]() {
                return !session.events.empty() || session.is_closed;
            });
            NextEvent next = { std::string(), !session.events.empty(), session.is_closed };
            if (next.has_event)
            {
                next.event = session.events.front();
                session.events.pop_front();
            }
            return next;
        }

        template <typename Job>
        void post(Job&& job)
        {
            const std::lock_guard<std::mutex> lock(_mutex);
            _workers.emplace_back(std::forward<Job>(job));
        }

        void join()
        {
            std::vector<std::thread> taken;
            {
                const std::lock_guard<std::mutex> lock(_mutex);
                taken.swap(_workers);
            }
            for (std::thread& worker : taken) { worker.join(); }
        }

        std::atomic<int> works_started{ 0 };

    private:
        std::mutex                        _mutex;
        std::condition_variable           _changed;
        std::map<std::string, McpSession> _sessions;
        std::vector<std::thread>          _workers;
        int                               _next_id = 0;
    };

    McpApp& appOf(KiottyNetAppContext context)
    {
        return *static_cast<McpApp*>(context);
    }

    std::string sessionOf(const KiottyNetHttpRequest* request)
    {
        return textOf(KiottyNet_findHttpHeader(request, "Mcp-Session-Id"));
    }

    void onPost(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle r)
    {
        McpApp&                app = appOf(app_context);
        KiottyNet_HttpResponse response(r);
        const KiottyNetSpan    accept = KiottyNet_findHttpHeader(request, "Accept");
        const bool is_accepted = contains(accept, "application/json") && contains(accept, "text/event-stream");
        if (!is_accepted)
        {
            response.status(KIOTTY_NET_HTTP_STATUS_NOT_ACCEPTABLE);
            return;
        }
        const bool is_initialize = contains(request->body, "\"method\":\"initialize\"");
        if (!is_initialize && !app.has(sessionOf(request)))
        {
            response.status(KIOTTY_NET_HTTP_STATUS_NOT_FOUND);
            return;
        }
        if (contains(request->body, "\"method\":\"notifications/"))
        {
            response.status(KIOTTY_NET_HTTP_STATUS_ACCEPTED);
            return;
        }
        response.startWork();
    }

    void onPostWork(KiottyNetAppContext app_context,
                    const KiottyNetHttpRequest* request,
                    KiottyNetHttpResponseHandle r,
                    KiottyNetHttpWorkHandle w)
    {
        McpApp& app = appOf(app_context);
        app.works_started.fetch_add(1);
        const std::string body = textOf(request->body);
        KiottyNet_HttpWork work(w);
        app.post([&app, body, r, work = std::move(work)]() mutable {
            KiottyNet_HttpResponse response(r);
            if (body.find("\"method\":\"initialize\"") != std::string::npos)
            {
                const std::string id = app.create();
                response.header("Mcp-Session-Id", id.c_str());
                response.body(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf(std::string("{\"result\":\"ready\"}")));
                work.finishWithResponse();
                return;
            }
            if (body.find("\"method\":\"tools/call\"") == std::string::npos)
            {
                response.body(KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, spanOf(std::string("{\"result\":\"pong\"}")));
                work.finishWithResponse();
                return;
            }
            response.bodyStream(KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
            for (int step = 1; step <= 2; ++step)
            {
                const std::string event = "event: message\ndata: {\"progress\":" + std::to_string(step) + "}\n\n";
                if (work.write(spanOf(event), WAIT_MS).code != KIOTTY_NET_CODE_SUCCESS)
                {
                    work.finishWithClose();
                    return;
                }
            }
            work.write(spanOf(std::string("event: message\ndata: {\"result\":\"done\"}\n\n")), WAIT_MS);
            work.finishWithResponse();
        });
    }

    void onGet(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle r)
    {
        McpApp&                app = appOf(app_context);
        KiottyNet_HttpResponse response(r);
        if (!contains(KiottyNet_findHttpHeader(request, "Accept"), "text/event-stream"))
        {
            response.status(KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED);
            return;
        }
        if (!app.has(sessionOf(request)))
        {
            response.status(KIOTTY_NET_HTTP_STATUS_NOT_FOUND);
            return;
        }
        response.header("Cache-Control", "no-cache");
        response.bodyStream(KIOTTY_NET_HTTP_CONTENT_TYPE_EVENT_STREAM);
        response.startWork();
    }

    void onGetWork(KiottyNetAppContext app_context,
                   const KiottyNetHttpRequest* request,
                   KiottyNetHttpResponseHandle,
                   KiottyNetHttpWorkHandle w)
    {
        McpApp& app = appOf(app_context);
        app.works_started.fetch_add(1);
        const std::string session = sessionOf(request);
        KiottyNet_HttpWork work(w);
        app.post([&app, session, work = std::move(work)]() mutable {
            if (!app.attachStream(session))
            {
                work.finishWithClose();
                return;
            }
            for (;;)
            {
                const NextEvent next = app.waitFor(session, PING_MS);
                if (!next.has_event && next.is_closed)
                {
                    work.finishWithResponse();
                    return;
                }
                const std::string event = next.has_event ? next.event : std::string(": ping\n\n");
                if (work.write(spanOf(event), WAIT_MS).code != KIOTTY_NET_CODE_SUCCESS)
                {
                    work.finishWithClose();
                    return;
                }
            }
        });
    }

    void onDelete(KiottyNetAppContext app_context, const KiottyNetHttpRequest* request, KiottyNetHttpResponseHandle r)
    {
        appOf(app_context).remove(sessionOf(request));
        KiottyNet_HttpResponse(r).status(KIOTTY_NET_HTTP_STATUS_NO_CONTENT);
    }

    struct Response
    {
        int         status;
        std::string head;
        std::string body;
    };

    bool readUntil(kiotty_test::LoopbackPeer& peer, std::string& text, const char* end)
    {
        const size_t end_length = std::strlen(end);
        text.clear();
        while (text.size() < end_length || text.compare(text.size() - end_length, end_length, end) != 0)
        {
            char next = 0;
            if (!peer.recvExactly(&next, 1, WAIT_MS)) { return false; }
            text.push_back(next);
        }
        return true;
    }

    std::string fieldOf(const std::string& head, const std::string& name)
    {
        const std::string key = "\r\n" + name + ": ";
        const size_t start = head.find(key);
        if (start == std::string::npos) { return std::string(); }
        const size_t value = start + key.size();
        return head.substr(value, head.find("\r\n", value) - value);
    }

    Response readHead(kiotty_test::LoopbackPeer& peer)
    {
        Response response = { -1, std::string(), std::string() };
        if (!readUntil(peer, response.head, "\r\n\r\n")) { return response; }
        response.status = std::atoi(response.head.substr(9, 3).c_str());
        return response;
    }

    Response readResponse(kiotty_test::LoopbackPeer& peer)
    {
        Response response = readHead(peer);
        const size_t length = std::strtoul(fieldOf(response.head, "Content-Length").c_str(), nullptr, 10);
        response.body.resize(length);
        if (length > 0 && !peer.recvExactly(&response.body[0], length, WAIT_MS)) { response.status = -1; }
        return response;
    }

    bool readChunk(kiotty_test::LoopbackPeer& peer, std::string& chunk)
    {
        std::string line;
        chunk.clear();
        if (!readUntil(peer, line, "\r\n")) { return false; }
        const size_t length = std::strtoul(line.c_str(), nullptr, 16);
        if (length == 0) { return readUntil(peer, line, "\r\n") && line == "\r\n"; }
        chunk.resize(length);
        if (!peer.recvExactly(&chunk[0], length, WAIT_MS)) { return false; }
        return readUntil(peer, line, "\r\n") && line == "\r\n";
    }

    std::string nextEvent(kiotty_test::LoopbackPeer& peer)
    {
        std::string chunk;
        while (readChunk(peer, chunk))
        {
            if (chunk != ": ping\n\n") { return chunk; }
        }
        return "<lost>";
    }

    std::string postOf(const std::string& session, const std::string& body, const char* accept = BOTH_ACCEPT)
    {
        std::string request = "POST /mcp HTTP/1.1\r\n";
        request += accept;
        if (!session.empty()) { request += "Mcp-Session-Id: " + session + "\r\n"; }
        request += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        return request + body;
    }

    class HttpMcpTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            _root = fs::temp_directory_path() / ("kiotty_mcp_" + std::to_string(stamp));
            fs::create_directories(_root);
            std::ofstream(_root / "index.html", std::ios::binary) << "<p>device</p>";
            KiottyNet_ServerConfigurationBuilder(_configuration).maxClients(8).build();
            KiottyNet_HttpLimitsBuilder(_limits).bodyCapacity(256 * 1024).build();
            _server.reset(new KiottyNet_HttpServer(_context.handle(), _configuration, _limits));
            ASSERT_TRUE(static_cast<bool>(*_server));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, _server->post("/mcp", KiottyNet_makeHttpRoute(onPost, onPostWork)));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, _server->get("/mcp", KiottyNet_makeHttpRoute(onGet, onGetWork)));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, _server->del("/mcp", KiottyNet_makeHttpRoute(onDelete, nullptr)));
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, _server->serveDirectory("/", _root.string().c_str()));
            port = kiotty_test::unusedPort();
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, KiottyNet_initializeTcpAddress(&_address, LOOPBACK_HOST, port));
            _thread = std::thread([this]() { _server->run(_address, &app); });
            ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, _server->await(WAIT_MS));
        }

        void TearDown() override
        {
            if (_server) { _server->close(); }
            if (_thread.joinable()) { _thread.join(); }
            app.join();
            _server.reset();
            std::error_code ignored;
            fs::remove_all(_root, ignored);
        }

        std::string initialize(kiotty_test::LoopbackPeer& peer)
        {
            EXPECT_TRUE(peer.connectTcp(port));
            const std::string request = postOf("", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}");
            EXPECT_TRUE(peer.sendAll(request.data(), request.size()));
            const Response response = readResponse(peer);
            EXPECT_EQ(200, response.status);
            EXPECT_EQ("{\"result\":\"ready\"}", response.body);
            return fieldOf(response.head, "Mcp-Session-Id");
        }

        Response ask(kiotty_test::LoopbackPeer& peer, const std::string& request)
        {
            EXPECT_TRUE(peer.sendAll(request.data(), request.size()));
            return readResponse(peer);
        }

        McpApp   app;
        uint16_t port = 0;

    private:
        KiottyNet_Context                     _context;
        KiottyNetServerConfiguration          _configuration{};
        KiottyNetHttpLimits                   _limits{};
        std::unique_ptr<KiottyNet_HttpServer> _server;
        KiottyNetAddress                      _address{};
        std::thread                           _thread;
        fs::path                              _root;
    };
}

TEST_F(HttpMcpTest, InitializeAnswersJsonWithASessionHeader)
{
    kiotty_test::LoopbackPeer peer;
    const std::string session = initialize(peer);
    EXPECT_EQ("s-1", session);
    const Response pong = ask(peer, postOf(session, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}"));
    EXPECT_EQ(200, pong.status);
    EXPECT_EQ("application/json", fieldOf(pong.head, "Content-Type"));
    EXPECT_EQ("{\"result\":\"pong\"}", pong.body);
}

TEST_F(HttpMcpTest, RequestsOutsideTheProtocolGetTheirStatus)
{
    kiotty_test::LoopbackPeer peer;
    const std::string session = initialize(peer);
    const std::string call = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}";
    EXPECT_EQ(406, ask(peer, postOf(session, call, "Accept: application/json\r\n")).status);
    EXPECT_EQ(404, ask(peer, postOf("s-99", call)).status);
    const std::string notice = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    EXPECT_EQ(202, ask(peer, postOf(session, notice)).status);
    EXPECT_EQ(405, ask(peer, "GET /mcp HTTP/1.1\r\nAccept: application/json\r\n\r\n").status);
    EXPECT_EQ(1, app.works_started.load());
}

TEST_F(HttpMcpTest, ToolCallStreamsProgressThenTheResultAndKeepsAlive)
{
    kiotty_test::LoopbackPeer peer;
    const std::string session = initialize(peer);
    const std::string request = postOf(session, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\"}");
    ASSERT_TRUE(peer.sendAll(request.data(), request.size()));
    const Response head = readHead(peer);
    EXPECT_EQ(200, head.status);
    EXPECT_EQ("chunked", fieldOf(head.head, "Transfer-Encoding"));
    EXPECT_EQ("text/event-stream; charset=utf-8", fieldOf(head.head, "Content-Type"));
    EXPECT_EQ("event: message\ndata: {\"progress\":1}\n\n", nextEvent(peer));
    EXPECT_EQ("event: message\ndata: {\"progress\":2}\n\n", nextEvent(peer));
    EXPECT_EQ("event: message\ndata: {\"result\":\"done\"}\n\n", nextEvent(peer));
    EXPECT_EQ("", nextEvent(peer));
    const Response pong = ask(peer, postOf(session, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"ping\"}"));
    EXPECT_EQ("{\"result\":\"pong\"}", pong.body);
}

TEST_F(HttpMcpTest, GetStreamCarriesAppEventsUntilDeleteEndsIt)
{
    kiotty_test::LoopbackPeer control;
    const std::string session = initialize(control);
    kiotty_test::LoopbackPeer stream;
    ASSERT_TRUE(stream.connectTcp(port));
    const std::string open = "GET /mcp HTTP/1.1\r\nAccept: text/event-stream\r\nMcp-Session-Id: " + session + "\r\n\r\n";
    ASSERT_TRUE(stream.sendAll(open.data(), open.size()));
    const Response head = readHead(stream);
    EXPECT_EQ(200, head.status);
    EXPECT_EQ("no-cache", fieldOf(head.head, "Cache-Control"));
    app.push(session, "event: message\ndata: {\"method\":\"notifications/tools/list_changed\"}\n\n");
    EXPECT_EQ("event: message\ndata: {\"method\":\"notifications/tools/list_changed\"}\n\n", nextEvent(stream));
    const std::string remove = "DELETE /mcp HTTP/1.1\r\nMcp-Session-Id: " + session + "\r\n\r\n";
    EXPECT_EQ(204, ask(control, remove).status);
    EXPECT_EQ("", nextEvent(stream));
    const Response page = ask(stream, "GET /index.html HTTP/1.1\r\n\r\n");
    EXPECT_EQ(200, page.status);
    EXPECT_EQ("<p>device</p>", page.body);
}

TEST_F(HttpMcpTest, HeadOnTheStreamSendsHeadersWithoutWork)
{
    kiotty_test::LoopbackPeer peer;
    const std::string session = initialize(peer);
    const std::string head = "HEAD /mcp HTTP/1.1\r\nAccept: text/event-stream\r\nMcp-Session-Id: " + session + "\r\n\r\n";
    ASSERT_TRUE(peer.sendAll(head.data(), head.size()));
    const Response response = readHead(peer);
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("chunked", fieldOf(response.head, "Transfer-Encoding"));
    EXPECT_EQ(200, ask(peer, "GET /index.html HTTP/1.1\r\n\r\n").status);
    EXPECT_EQ(1, app.works_started.load());
}

#endif
