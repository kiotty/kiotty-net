#include "kiotty_net_base.h"

#include "support/kiotty_test_peer.h"

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(__linux__)

namespace
{
    const uint32_t WAIT_MS = 10000;
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const size_t   MESSAGE_BYTES = 64;
    const size_t   CHUNK_BYTES = 16384;
    const size_t   HEAVY_BYTES = 1024 * 1024;
    const uint64_t FILE_BYTES = 64ULL * 1024 * 1024;

    enum class Role : uint8_t
    {
        ECHO,
        DRAIN,
        WORK
    };

    struct App
    {
        explicit App(Role app_role)
            : role(app_role)
            , ready(0)
            , disconnected(0)
            , work_started(0)
            , drained(0)
            , stream_owner(nullptr)
            , stream_id(0)
            , echo()
        {
        }

        Role                            role;
        std::atomic<int>                ready;
        std::atomic<int>                disconnected;
        std::atomic<int>                work_started;
        std::atomic<uint64_t>           drained;
        std::atomic<KiottyNetEndpoint*> stream_owner;
        std::atomic<int64_t>            stream_id;
        std::vector<char>               echo;
    };

    struct Slot
    {
        App* app;
        char message[MESSAGE_BYTES];
    };

    App& appOf(void* context)
    {
        return *static_cast<App*>(context);
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        Slot* slot = new Slot{ static_cast<App*>(app_context), {} };
        return KiottyNet_acceptConnection(slot);
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)connection;
        App& app = *static_cast<Slot*>(context)->app;
        app.ready.fetch_add(1);
        switch (app.role)
        {
        case Role::ECHO:
            return KiottyNet_thenRead(MESSAGE_BYTES, 0);
        case Role::DRAIN:
            return KiottyNet_thenRead(CHUNK_BYTES, 0);
        case Role::WORK:
            return KiottyNet_thenWork();
        }
        return KiottyNet_thenClose(KIOTTY_NET_CODE_PROTOCOL_ERROR);
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)connection;
        Slot& slot = *static_cast<Slot*>(context);
        App& app = *slot.app;
        if (app.role == Role::DRAIN)
        {
            app.drained.fetch_add(data.length);
            return KiottyNet_thenRead(CHUNK_BYTES, 0);
        }
        std::memcpy(slot.message, data.data, data.length);
        KiottyNetSpan echo = { slot.message, data.length };
        return KiottyNet_thenWrite(echo, 0);
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length)
    {
        (void)context;
        (void)connection;
        (void)length;
        return KiottyNet_thenRead(MESSAGE_BYTES, 0);
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)context;
        (void)connection;
        return KiottyNet_thenHold(0);
    }

    void onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        App& app = *static_cast<Slot*>(context)->app;
        app.stream_owner.store(stream.owner);
        app.stream_id.store(stream.id);
        app.work_started.fetch_add(1);
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
    {
        (void)connection;
        (void)reason;
        delete static_cast<Slot*>(context);
        appOf(app_context).disconnected.fetch_add(1);
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)app_context;
        (void)info;
        (void)code;
        (void)message;
    }

    const KiottyNetConnectionHandler HANDLER = { &onConnected, &onReady, &onRead, &onWritten, &onTimeout, &onWorkStarted, &onDisconnected, &onError };

    std::string certPath(const char* name)
    {
        return std::string(KIOTTY_BENCH_CERT_DIR) + "/" + name;
    }

    class BenchServer
    {
    public:
        BenchServer(Role role, bool tls)
            : app(role)
            , port(kiotty_test::unusedPort())
            , _context(KiottyNet_initializeContext().handle)
            , _server(nullptr)
            , _thread()
        {
            KiottyNetServerConfiguration configuration;
            (void)KiottyNet_initializeTcpServerConfiguration(&configuration, LOOPBACK_HOST, port);
            (void)KiottyNet_configureServerBufferCapacity(&configuration, CHUNK_BYTES, CHUNK_BYTES, CHUNK_BYTES);
            KiottyNetTlsServerConfiguration secure;
            const std::string cert = certPath("server.pem");
            const std::string key = certPath("server.key");
            (void)KiottyNet_initializeTlsServerConfiguration(&secure, cert.c_str(), key.c_str(), nullptr);
            _server = KiottyNet_createServer(_context, &configuration, tls ? &secure : nullptr).handle;
            if (_server == nullptr) { return; }
            _thread = std::thread([this]() { (void)KiottyNet_runServer(_server, &HANDLER, &app); });
            (void)KiottyNet_awaitServer(_server, WAIT_MS);
        }

        ~BenchServer()
        {
            if (_server != nullptr)
            {
                KiottyNet_closeServer(_server);
                _thread.join();
                KiottyNet_destroyServer(_context, _server);
            }
            KiottyNet_releaseContext(_context);
        }

        BenchServer(const BenchServer&) = delete;
        BenchServer& operator=(const BenchServer&) = delete;

        bool valid() const
        {
            return _server != nullptr;
        }

        KiottyNetServerHandle server() const
        {
            return _server;
        }

        KiottyNetContext context() const
        {
            return _context;
        }

        KiottyNetConnectionStream stream() const
        {
            return KiottyNetConnectionStream{ app.stream_owner.load(), app.stream_id.load() };
        }

        App      app;
        uint16_t port;

    private:
        KiottyNetContext      _context;
        KiottyNetServerHandle _server;
        std::thread           _thread;
    };

    template <typename Predicate>
    bool spinUntil(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(WAIT_MS);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline) { return false; }
            std::this_thread::yield();
        }
        return true;
    }

    bool waitFor(const std::atomic<int>& counter, int target)
    {
        return spinUntil([&]() { return counter.load() >= target; });
    }

    KiottyNetStreamClientHandle createStreamClient(BenchServer& bench, bool tls)
    {
        KiottyNetClientConfiguration configuration;
        (void)KiottyNet_initializeTcpClientConfiguration(&configuration, LOOPBACK_HOST, bench.port);
        KiottyNetTlsClientConfiguration secure;
        const std::string trust = certPath("ca.pem");
        (void)KiottyNet_initializeTlsClientConfiguration(&secure);
        (void)KiottyNet_configureTlsClientTrustStore(&secure, trust.c_str());
        return KiottyNet_createStreamClient(bench.context(), &configuration, tls ? &secure : nullptr).handle;
    }

    bool sendAndReceive(kiotty_test::LoopbackPeer& peer, const char* message, char* reply)
    {
        if (!peer.sendAll(message, MESSAGE_BYTES)) { return false; }
        return peer.recvExactly(reply, MESSAGE_BYTES, WAIT_MS);
    }

    bool readAll(kiotty_test::LoopbackPeer& peer, uint64_t total)
    {
        std::vector<char> buffer(HEAVY_BYTES);
        uint64_t left = total;
        while (left > 0)
        {
            const size_t step = left < buffer.size() ? static_cast<size_t>(left) : buffer.size();
            if (!peer.recvExactly(buffer.data(), step, WAIT_MS)) { return false; }
            left -= step;
        }
        return true;
    }
}

static void EchoRoundtrip(benchmark::State& state)
{
    BenchServer bench(Role::ECHO, false);
    const int clients = static_cast<int>(state.range(0));
    std::vector<std::unique_ptr<kiotty_test::LoopbackPeer>> peers;
    for (int i = 0; i < clients; ++i)
    {
        peers.push_back(std::make_unique<kiotty_test::LoopbackPeer>());
        if (!peers.back()->connectTcp(bench.port)) { state.SkipWithError("connect failed"); return; }
    }
    if (!waitFor(bench.app.ready, clients)) { state.SkipWithError("not ready"); return; }
    char message[MESSAGE_BYTES] = {};
    char reply[MESSAGE_BYTES] = {};
    for (auto _ : state)
    {
        for (auto& peer : peers) { (void)peer->sendAll(message, sizeof(message)); }
        for (auto& peer : peers)
        {
            if (!peer->recvExactly(reply, sizeof(reply), WAIT_MS)) { state.SkipWithError("echo lost"); return; }
        }
    }
    state.SetItemsProcessed(state.iterations() * clients);
    state.SetBytesProcessed(state.iterations() * clients * static_cast<int64_t>(MESSAGE_BYTES));
}
BENCHMARK(EchoRoundtrip)->Arg(1)->Arg(128)->UseRealTime();

static void ReadHeavy(benchmark::State& state)
{
    BenchServer bench(Role::DRAIN, false);
    kiotty_test::LoopbackPeer peer;
    if (!peer.connectTcp(bench.port) || !waitFor(bench.app.ready, 1)) { state.SkipWithError("connect failed"); return; }
    std::vector<char> payload(HEAVY_BYTES, 'r');
    uint64_t expected = 0;
    for (auto _ : state)
    {
        (void)peer.sendAll(payload.data(), payload.size());
        expected += payload.size();
        const bool drained = spinUntil([&]() { return bench.app.drained.load() >= expected; });
        if (!drained) { state.SkipWithError("drain stalled"); return; }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(HEAVY_BYTES));
}
BENCHMARK(ReadHeavy)->UseRealTime();

static void WriteHeavy(benchmark::State& state)
{
    BenchServer bench(Role::WORK, false);
    kiotty_test::LoopbackPeer peer;
    if (!peer.connectTcp(bench.port) || !waitFor(bench.app.work_started, 1)) { state.SkipWithError("connect failed"); return; }
    const KiottyNetConnectionStream stream = bench.stream();
    std::vector<char> payload(HEAVY_BYTES, 'w');
    const KiottyNetSpan data = { payload.data(), payload.size() };
    for (auto _ : state)
    {
        std::thread reader([&]() { (void)readAll(peer, HEAVY_BYTES); });
        const KiottyNetResult written = KiottyNet_writeConnectionStream(stream, data, WAIT_MS);
        reader.join();
        if (written.code != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("write failed"); break; }
    }
    (void)KiottyNet_closeConnectionStream(stream, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS));
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(HEAVY_BYTES));
}
BENCHMARK(WriteHeavy)->UseRealTime();

static void BroadcastFanOut(benchmark::State& state)
{
    BenchServer bench(Role::ECHO, false);
    const int clients = static_cast<int>(state.range(0));
    std::vector<std::unique_ptr<kiotty_test::LoopbackPeer>> peers;
    for (int i = 0; i < clients; ++i)
    {
        peers.push_back(std::make_unique<kiotty_test::LoopbackPeer>());
        if (!peers.back()->connectTcp(bench.port)) { state.SkipWithError("connect failed"); return; }
    }
    if (!waitFor(bench.app.ready, clients)) { state.SkipWithError("not ready"); return; }
    char message[MESSAGE_BYTES] = {};
    char reply[MESSAGE_BYTES] = {};
    const KiottyNetSpan data = { message, sizeof(message) };
    for (auto _ : state)
    {
        if (KiottyNet_broadcastServer(bench.server(), data) != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("broadcast refused"); return; }
        for (auto& peer : peers)
        {
            if (!peer->recvExactly(reply, sizeof(reply), WAIT_MS)) { state.SkipWithError("broadcast lost"); return; }
        }
    }
    state.SetItemsProcessed(state.iterations() * clients);
}
BENCHMARK(BroadcastFanOut)->Arg(16)->Arg(128)->UseRealTime();

static void ConnectionChurn(benchmark::State& state)
{
    BenchServer bench(Role::ECHO, false);
    int cycles = 0;
    char message[MESSAGE_BYTES] = {};
    char reply[MESSAGE_BYTES] = {};
    for (auto _ : state)
    {
        {
            kiotty_test::LoopbackPeer peer;
            if (!peer.connectTcp(bench.port) || !sendAndReceive(peer, message, reply)) { state.SkipWithError("cycle failed"); return; }
        }
        cycles += 1;
        if (!waitFor(bench.app.disconnected, cycles)) { state.SkipWithError("disconnect stalled"); return; }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(ConnectionChurn)->UseRealTime();

static void WorkHandoff(benchmark::State& state)
{
    BenchServer bench(Role::WORK, false);
    int cycles = 0;
    for (auto _ : state)
    {
        kiotty_test::LoopbackPeer peer;
        if (!peer.connectTcp(bench.port)) { state.SkipWithError("connect failed"); return; }
        cycles += 1;
        if (!waitFor(bench.app.work_started, cycles)) { state.SkipWithError("work not started"); return; }
        (void)KiottyNet_closeConnectionStream(bench.stream(), KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS));
        if (!waitFor(bench.app.disconnected, cycles)) { state.SkipWithError("disconnect stalled"); return; }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(WorkHandoff)->UseRealTime();

static void StreamEcho(benchmark::State& state)
{
    const bool tls = state.range(0) != 0;
    BenchServer bench(Role::ECHO, tls);
    if (!bench.valid()) { state.SkipWithError("server not created (TLS off?)"); return; }
    KiottyNetStreamClientHandle client = createStreamClient(bench, tls);
    if (client == nullptr || KiottyNet_connectStreamClient(client) != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("connect failed"); return; }
    char message[MESSAGE_BYTES] = {};
    char reply[MESSAGE_BYTES] = {};
    const KiottyNetSpan data = { message, sizeof(message) };
    for (auto _ : state)
    {
        (void)KiottyNet_writeStreamClient(client, data, WAIT_MS);
        size_t got = 0;
        while (got < sizeof(reply))
        {
            const KiottyNetResult read = KiottyNet_readStreamClient(client, reply + got, sizeof(reply) - got, WAIT_MS);
            if (read.code != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("read failed"); return; }
            got += static_cast<size_t>(read.bytes);
        }
    }
    KiottyNet_disconnectStreamClient(client);
    KiottyNet_destroyStreamClient(bench.context(), client);
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(MESSAGE_BYTES));
}
BENCHMARK(StreamEcho)->ArgName("tls")->Arg(0)->Arg(1)->UseRealTime();

static void TlsHandshakeRate(benchmark::State& state)
{
    BenchServer bench(Role::ECHO, true);
    if (!bench.valid()) { state.SkipWithError("server not created (TLS off?)"); return; }
    KiottyNetStreamClientHandle client = createStreamClient(bench, true);
    if (client == nullptr) { state.SkipWithError("client not created"); return; }
    for (auto _ : state)
    {
        if (KiottyNet_connectStreamClient(client) != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("handshake failed"); break; }
        KiottyNet_disconnectStreamClient(client);
    }
    KiottyNet_destroyStreamClient(bench.context(), client);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(TlsHandshakeRate)->UseRealTime();

static void StreamWriteFile64MiB(benchmark::State& state)
{
    const std::string path = "kiotty_bench_64mib.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        std::vector<char> block(HEAVY_BYTES, 'f');
        for (uint64_t i = 0; i < FILE_BYTES / HEAVY_BYTES; ++i) { file.write(block.data(), static_cast<std::streamsize>(block.size())); }
    }
    {
        BenchServer bench(Role::WORK, false);
        kiotty_test::LoopbackPeer peer;
        if (!peer.connectTcp(bench.port) || !waitFor(bench.app.work_started, 1)) { state.SkipWithError("connect failed"); return; }
        const KiottyNetConnectionStream stream = bench.stream();
        for (auto _ : state)
        {
            std::thread reader([&]() { (void)readAll(peer, FILE_BYTES); });
            const KiottyNetCode sent = KiottyNet_writeConnectionStreamFile(stream, path.c_str(), 0, FILE_BYTES, WAIT_MS);
            reader.join();
            if (sent != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("send file failed"); break; }
        }
        (void)KiottyNet_closeConnectionStream(stream, KiottyNet_thenClose(KIOTTY_NET_CODE_SUCCESS));
    }
    (void)std::remove(path.c_str());
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FILE_BYTES));
}
BENCHMARK(StreamWriteFile64MiB)->Iterations(4)->UseRealTime();

#endif
