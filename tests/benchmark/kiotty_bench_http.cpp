#include "kiotty_net_http.h"

#include "support/kiotty_test_peer.h"

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    namespace fs = std::filesystem;

    const char*    LOOPBACK_HOST = "127.0.0.1";
    const uint32_t WAIT_MS = 5000;
    const size_t   CHUNK_BYTES = 1024;
    const size_t   FILE_BYTES = 1024 * 1024;

    struct ChunkFeed
    {
        std::mutex              mutex;
        std::condition_variable changed;
        std::thread             worker;
        int                     wanted = 0;
        bool                    is_done = false;
    };

    void onHealth(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        const char text[] = "{\"ok\":true}";
        KiottyNet_setHttpBody(response, KIOTTY_NET_HTTP_CONTENT_TYPE_JSON, KiottyNetSpan{ text, sizeof(text) - 1 });
    }

    void onStream(KiottyNetAppContext, const KiottyNetHttpRequest*, KiottyNetHttpResponseHandle response)
    {
        KiottyNet_setHttpBodyStream(response, KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM);
        KiottyNet_startHttpWork(response);
    }

    void onStreamWork(KiottyNetAppContext app_context,
                      const KiottyNetHttpRequest*,
                      KiottyNetHttpResponseHandle,
                      KiottyNetHttpWorkHandle work)
    {
        ChunkFeed& feed = *static_cast<ChunkFeed*>(app_context);
        feed.worker = std::thread([&feed, work]() {
            const std::string chunk(CHUNK_BYTES, 'c');
            const KiottyNetSpan data = { chunk.data(), chunk.size() };
            for (;;)
            {
                std::unique_lock<std::mutex> lock(feed.mutex);
                feed.changed.wait(lock, [&feed]() { return feed.wanted > 0 || feed.is_done; });
                if (feed.is_done) { break; }
                feed.wanted -= 1;
                lock.unlock();
                if (KiottyNet_writeHttpWorkChunk(work, data, WAIT_MS).code != KIOTTY_NET_CODE_SUCCESS) { break; }
            }
            KiottyNet_finishHttpWorkWithResponse(work);
        });
    }

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

    bool readLengthResponse(kiotty_test::LoopbackPeer& peer, std::string& head, std::string& body)
    {
        if (!readUntil(peer, head, "\r\n\r\n")) { return false; }
        const size_t key = head.find("Content-Length: ");
        const size_t length = std::strtoul(head.c_str() + key + 16, nullptr, 10);
        body.resize(length);
        return length == 0 || peer.recvExactly(&body[0], length, WAIT_MS);
    }

    class BenchHttpServer
    {
    public:
        BenchHttpServer()
        {
            context = KiottyNet_initializeContext().handle;
            KiottyNet_initializeHttpLimits(&limits);
            KiottyNet_initializeServerConfiguration(&configuration);
            server = KiottyNet_createHttpServer(context, &configuration, &limits).handle;
        }

        ~BenchHttpServer()
        {
            KiottyNet_closeHttpServer(server);
            if (thread.joinable()) { thread.join(); }
            KiottyNet_destroyHttpServer(context, server);
            KiottyNet_releaseContext(context);
        }

        bool start(KiottyNetAppContext app_context)
        {
            port = kiotty_test::unusedPort();
            KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port);
            thread = std::thread([this, app_context]() { KiottyNet_runHttpServer(server, &address, nullptr, app_context); });
            return KiottyNet_awaitHttpServer(server, WAIT_MS) == KIOTTY_NET_CODE_SUCCESS;
        }

        KiottyNetContext             context = nullptr;
        KiottyNetHttpServerHandle    server = nullptr;
        KiottyNetHttpLimits          limits{};
        KiottyNetServerConfiguration configuration{};
        KiottyNetAddress             address{};
        std::thread                  thread;
        uint16_t                     port = 0;
    };
}

static void HttpGetJsonRoundtrip(benchmark::State& state)
{
    BenchHttpServer bench;
    KiottyNet_addHttpRoute(bench.server, KIOTTY_NET_HTTP_METHOD_GET, "/health", KiottyNet_makeHttpRoute(onHealth, nullptr));
    if (!bench.start(nullptr)) { state.SkipWithError("run failed"); return; }
    kiotty_test::LoopbackPeer peer;
    if (!peer.connectTcp(bench.port)) { state.SkipWithError("connect failed"); return; }
    const std::string request = "GET /health HTTP/1.1\r\nHost: a\r\n\r\n";
    std::string head;
    std::string body;
    for (auto _ : state)
    {
        peer.sendAll(request.data(), request.size());
        if (!readLengthResponse(peer, head, body)) { state.SkipWithError("read failed"); break; }
    }
}
BENCHMARK(HttpGetJsonRoundtrip)->UseRealTime();

static void HttpWorkChunkLatency(benchmark::State& state)
{
    BenchHttpServer bench;
    ChunkFeed feed;
    KiottyNet_addHttpRoute(bench.server, KIOTTY_NET_HTTP_METHOD_GET, "/stream", KiottyNet_makeHttpRoute(onStream, onStreamWork));
    if (!bench.start(&feed)) { state.SkipWithError("run failed"); return; }
    kiotty_test::LoopbackPeer peer;
    if (!peer.connectTcp(bench.port)) { state.SkipWithError("connect failed"); return; }
    const std::string request = "GET /stream HTTP/1.1\r\n\r\n";
    peer.sendAll(request.data(), request.size());
    std::string line;
    std::string chunk(CHUNK_BYTES + 2, '\0');
    bool is_head_read = false;
    for (auto _ : state)
    {
        {
            const std::lock_guard<std::mutex> lock(feed.mutex);
            feed.wanted += 1;
        }
        feed.changed.notify_one();
        const bool is_read = (is_head_read || readUntil(peer, line, "\r\n\r\n")) && readUntil(peer, line, "\r\n")
                             && peer.recvExactly(&chunk[0], chunk.size(), WAIT_MS);
        is_head_read = true;
        if (!is_read) { state.SkipWithError("read failed"); break; }
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * CHUNK_BYTES));
    {
        const std::lock_guard<std::mutex> lock(feed.mutex);
        feed.is_done = true;
    }
    feed.changed.notify_one();
    if (feed.worker.joinable()) { feed.worker.join(); }
}
BENCHMARK(HttpWorkChunkLatency)->Iterations(1000)->UseRealTime();

static void HttpStaticFile1MiB(benchmark::State& state)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() / ("kiotty_bench_" + std::to_string(stamp) + ".bin");
    {
        std::ofstream file(path, std::ios::binary);
        const std::string content(FILE_BYTES, 'f');
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    {
        BenchHttpServer bench;
        KiottyNet_serveHttpFile(bench.server, "/file.bin", path.string().c_str());
        if (!bench.start(nullptr)) { state.SkipWithError("run failed"); return; }
        kiotty_test::LoopbackPeer peer;
        if (!peer.connectTcp(bench.port)) { state.SkipWithError("connect failed"); return; }
        const std::string request = "GET /file.bin HTTP/1.1\r\n\r\n";
        std::string head;
        std::string body;
        for (auto _ : state)
        {
            peer.sendAll(request.data(), request.size());
            if (!readLengthResponse(peer, head, body) || body.size() != FILE_BYTES) { state.SkipWithError("read failed"); break; }
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * FILE_BYTES));
    }
    std::error_code ignored;
    fs::remove(path, ignored);
}
BENCHMARK(HttpStaticFile1MiB)->UseRealTime();
