#include "kiotty_net_base.h"
#include "kiotty_net_statemachine.h"

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

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

namespace
{
    const uint32_t WAIT_MS = 10000;
    const char*    LOOPBACK_HOST = "127.0.0.1";
    const size_t   MESSAGE_BYTES = 64;
    const size_t   CHUNK_BYTES = 16384;
    const size_t   HEAVY_BYTES = 1024 * 1024;
    const uint64_t FILE_BYTES = 64ULL * 1024 * 1024;
    const size_t   DEFAULT_MAX_CLIENTS = 1024;

    enum class Role : uint8_t
    {
        ECHO,
        STATEFUL_ECHO,
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
            , chunk(CHUNK_BYTES)
        {
        }

        Role                            role;
        std::atomic<int>                ready;
        std::atomic<int>                disconnected;
        std::atomic<int>                work_started;
        std::atomic<uint64_t>           drained;
        std::atomic<KiottyNetEndpoint*> stream_owner;
        std::atomic<int64_t>            stream_id;
        size_t                          chunk;
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
        case Role::STATEFUL_ECHO:
            return KiottyNet_thenRead(MESSAGE_BYTES, 0);
        case Role::DRAIN:
            return KiottyNet_thenRead(app.chunk, 0);
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
            return KiottyNet_thenRead(app.chunk, 0);
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

    enum class EchoState
    {
        Reading,
        Writing
    };

    typedef KiottyNet_Transition<EchoState> EchoNext;

    KiottyNetTransition statefulConnected(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent)
    {
        return EchoNext::moveToNextState(EchoState::Reading, onReady(context, connection));
    }

    KiottyNetTransition statefulRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetConnectionEvent event)
    {
        return EchoNext::moveToNextState(EchoState::Writing, onRead(context, connection, event.data));
    }

    KiottyNetTransition statefulWritten(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return EchoNext::moveToNextState(EchoState::Reading, KiottyNet_thenRead(MESSAGE_BYTES, 0));
    }

    KiottyNetTransition statefulIgnored(KiottyNetConnectionContext, KiottyNetConnectionHandle, KiottyNetConnectionEvent)
    {
        return EchoNext::moveToNextState(EchoState::Reading, KiottyNet_thenHold(0));
    }

    KiottyNetConnectionContext statefulCreate(KiottyNetAppContext app_context, KiottyNetConnectionInfo)
    {
        return new Slot{ static_cast<App*>(app_context), {} };
    }

    void statefulRelease(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle)
    {
        delete static_cast<Slot*>(context);
        appOf(app_context).disconnected.fetch_add(1);
    }

    constexpr auto ECHO_RULES = KiottyNet_rules(
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED).then(statefulConnected).to(EchoState::Reading),
        KiottyNet_from(EchoState::Reading).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_READ).then(statefulRead).to(EchoState::Writing),
        KiottyNet_from(EchoState::Writing).when(KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN).then(statefulWritten).to(EchoState::Reading),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT).then(statefulIgnored),
        KiottyNet_when(KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED).then(statefulIgnored));
    constexpr KiottyNetStateMachine ECHO_MACHINE = KiottyNet_makeStateMachine(ECHO_RULES, EchoState::Reading);

    struct BenchTarget
    {
        KiottyNetAddress                       address;
        const KiottyNetTlsServerConfiguration* tls;
    };

    KiottyNetCode runBenchServer(KiottyNetServerHandle server, const BenchTarget& target, App& app, KiottyNetStatefulAppContext& stateful)
    {
        if (app.role != Role::STATEFUL_ECHO) { return KiottyNet_runServer(server, &target.address, target.tls, &HANDLER, &app); }
        stateful = KiottyNetStatefulAppContext{ &app, &ECHO_MACHINE, statefulCreate, statefulRelease, nullptr };
        return KiottyNet_runStatefulServer(server, &target.address, target.tls, &stateful);
    }

    std::string certPath(const char* name)
    {
        return std::string(KIOTTY_BENCH_CERT_DIR) + "/" + name;
    }

    KiottyNetAddress loopbackAddress(uint16_t port)
    {
        KiottyNetAddress address;
        (void)KiottyNet_initializeTcpAddress(&address, LOOPBACK_HOST, port);
        return address;
    }

    class BenchServer
    {
    public:
        BenchServer(Role role, bool tls, size_t receive_capacity = CHUNK_BYTES, size_t max_clients = DEFAULT_MAX_CLIENTS)
            : app(role)
            , port(kiotty_test::unusedPort())
            , _context(KiottyNet_initializeContext().handle)
            , _server(nullptr)
            , _secure()
            , _target{ loopbackAddress(port), nullptr }
            , _started(KIOTTY_NET_CODE_TIMEOUT)
            , _stateful()
            , _thread()
        {
            KiottyNetServerConfiguration configuration;
            (void)KiottyNet_initializeServerConfiguration(&configuration);
            (void)KiottyNet_configureServerMaxClients(&configuration, max_clients);
            app.chunk = receive_capacity;
            const KiottyNetCode sized = KiottyNet_configureServerBufferCapacity(&configuration, receive_capacity, CHUNK_BYTES, CHUNK_BYTES);
            if (sized != KIOTTY_NET_CODE_SUCCESS) { return; }
            const std::string cert = certPath("server.pem");
            const std::string key = certPath("server.key");
            (void)KiottyNet_initializeTlsServerConfiguration(&_secure, cert.c_str(), key.c_str(), nullptr);
            _target.tls = tls ? &_secure : nullptr;
            _server = KiottyNet_createServer(_context, &configuration).handle;
            if (_server == nullptr) { return; }
            _thread = std::thread([this]() { (void)runBenchServer(_server, _target, app, _stateful); });
            _started = KiottyNet_awaitServer(_server, WAIT_MS);
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
            return _server != nullptr && _started == KIOTTY_NET_CODE_SUCCESS;
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
        KiottyNetContext                _context;
        KiottyNetServerHandle           _server;
        KiottyNetTlsServerConfiguration _secure;
        BenchTarget                     _target;
        KiottyNetCode                   _started;
        KiottyNetStatefulAppContext     _stateful;
        std::thread                 _thread;
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

    KiottyNetStreamClientHandle createStreamClient(BenchServer& bench)
    {
        KiottyNetClientConfiguration configuration;
        (void)KiottyNet_initializeClientConfiguration(&configuration);
        return KiottyNet_createStreamClient(bench.context(), &configuration).handle;
    }

    KiottyNetCode connectStreamClient(KiottyNetStreamClientHandle client, uint16_t port, bool tls)
    {
        const KiottyNetAddress address = loopbackAddress(port);
        KiottyNetTlsClientConfiguration secure;
        const std::string trust = certPath("ca.pem");
        (void)KiottyNet_initializeTlsClientConfiguration(&secure);
        (void)KiottyNet_configureTlsClientTrustStore(&secure, trust.c_str());
        return KiottyNet_connectStreamClient(client, &address, tls ? &secure : nullptr);
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

static void runEchoRoundtrip(benchmark::State& state, Role role)
{
    BenchServer bench(role, false, CHUNK_BYTES, static_cast<size_t>(state.range(1)));
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
static void EchoRoundtrip(benchmark::State& state)
{
    runEchoRoundtrip(state, Role::ECHO);
}
BENCHMARK(EchoRoundtrip)->ArgNames({ "clients", "max_clients" })->Args({ 1, 16 })->Args({ 1, 1024 })->Args({ 128, 1024 })->UseRealTime();

static void EchoRoundtripStateful(benchmark::State& state)
{
    runEchoRoundtrip(state, Role::STATEFUL_ECHO);
}
BENCHMARK(EchoRoundtripStateful)->ArgNames({ "clients", "max_clients" })->Args({ 1, 16 })->Args({ 1, 1024 })->Args({ 128, 1024 })->UseRealTime();

static void ReadHeavy(benchmark::State& state)
{
    BenchServer bench(Role::DRAIN, false, static_cast<size_t>(state.range(0)), static_cast<size_t>(state.range(1)));
    if (!bench.valid()) { state.SkipWithError("receive capacity above the compile time maximum"); return; }
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
BENCHMARK(ReadHeavy)->ArgNames({ "receive", "max_clients" })->ArgsProduct({ { 16384, 65536 }, { 16, 1024 } })->UseRealTime();

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
    KiottyNetStreamClientHandle client = createStreamClient(bench);
    if (client == nullptr || connectStreamClient(client, bench.port, tls) != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("connect failed"); return; }
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
    KiottyNetStreamClientHandle client = createStreamClient(bench);
    if (client == nullptr) { state.SkipWithError("client not created"); return; }
    for (auto _ : state)
    {
        if (connectStreamClient(client, bench.port, true) != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("handshake failed"); break; }
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

namespace
{
    class BenchContext
    {
    public:
        BenchContext()
            : handle(KiottyNet_initializeContext().handle)
        {
        }

        ~BenchContext()
        {
            KiottyNet_releaseContext(handle);
        }

        BenchContext(const BenchContext&) = delete;
        BenchContext& operator=(const BenchContext&) = delete;

        KiottyNetContext handle;
    };

    KiottyNetClientConfiguration clientConfiguration(size_t receive_capacity, bool& sized)
    {
        KiottyNetClientConfiguration configuration;
        (void)KiottyNet_initializeClientConfiguration(&configuration);
        const KiottyNetCode code = KiottyNet_configureClientBufferCapacity(&configuration, receive_capacity, CHUNK_BYTES, CHUNK_BYTES);
        sized = code == KIOTTY_NET_CODE_SUCCESS;
        return configuration;
    }

    class BenchEventClient
    {
    public:
        BenchEventClient(KiottyNetContext context, uint16_t port, Role role, size_t receive_capacity)
            : app(role)
            , _context(context)
            , _client(nullptr)
            , _address(loopbackAddress(port))
            , _thread()
        {
            bool sized = false;
            const KiottyNetClientConfiguration configuration = clientConfiguration(receive_capacity, sized);
            if (!sized) { return; }
            app.chunk = receive_capacity;
            _client = KiottyNet_createEventClient(_context, &configuration).handle;
            if (_client == nullptr) { return; }
            _thread = std::thread([this]() { (void)KiottyNet_runEventClient(_client, &_address, nullptr, &HANDLER, &app); });
        }

        ~BenchEventClient()
        {
            if (_client == nullptr) { return; }
            KiottyNet_closeEventClient(_client);
            _thread.join();
            KiottyNet_destroyEventClient(_context, _client);
        }

        BenchEventClient(const BenchEventClient&) = delete;
        BenchEventClient& operator=(const BenchEventClient&) = delete;

        bool valid() const
        {
            return _client != nullptr;
        }

        KiottyNetConnectionStream stream() const
        {
            return KiottyNetConnectionStream{ app.stream_owner.load(), app.stream_id.load() };
        }

        App app;

    private:
        KiottyNetContext           _context;
        KiottyNetEventClientHandle _client;
        KiottyNetAddress           _address;
        std::thread                _thread;
    };

    class BenchStreamClient
    {
    public:
        BenchStreamClient(KiottyNetContext context, uint16_t port, size_t receive_capacity)
            : _context(context)
            , _client(nullptr)
        {
            bool sized = false;
            const KiottyNetClientConfiguration configuration = clientConfiguration(receive_capacity, sized);
            if (!sized) { return; }
            _client = KiottyNet_createStreamClient(_context, &configuration).handle;
            if (_client == nullptr) { return; }
            if (connectStreamClient(_client, port, false) == KIOTTY_NET_CODE_SUCCESS) { return; }
            KiottyNet_destroyStreamClient(_context, _client);
            _client = nullptr;
        }

        ~BenchStreamClient()
        {
            if (_client == nullptr) { return; }
            KiottyNet_disconnectStreamClient(_client);
            KiottyNet_destroyStreamClient(_context, _client);
        }

        BenchStreamClient(const BenchStreamClient&) = delete;
        BenchStreamClient& operator=(const BenchStreamClient&) = delete;

        KiottyNetStreamClientHandle get() const
        {
            return _client;
        }

    private:
        KiottyNetContext            _context;
        KiottyNetStreamClientHandle _client;
    };

    bool writeAll(kiotty_test::LoopbackPeer& peer, const std::vector<char>& payload)
    {
        return peer.sendAll(payload.data(), payload.size());
    }
}

static void EventClientReadHeavy(benchmark::State& state)
{
    BenchContext context;
    kiotty_test::LoopbackPeer peer;
    const uint16_t port = kiotty_test::unusedPort();
    if (!peer.listenTcp(port)) { state.SkipWithError("listen failed"); return; }
    BenchEventClient client(context.handle, port, Role::DRAIN, static_cast<size_t>(state.range(0)));
    if (!client.valid()) { state.SkipWithError("receive capacity above the compile time maximum"); return; }
    if (!peer.accept(WAIT_MS) || !waitFor(client.app.ready, 1)) { state.SkipWithError("connect failed"); return; }
    std::vector<char> payload(HEAVY_BYTES, 'r');
    uint64_t expected = 0;
    for (auto _ : state)
    {
        (void)writeAll(peer, payload);
        expected += payload.size();
        const bool drained = spinUntil([&]() { return client.app.drained.load() >= expected; });
        if (!drained) { state.SkipWithError("drain stalled"); return; }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(HEAVY_BYTES));
}
BENCHMARK(EventClientReadHeavy)->ArgName("receive")->Arg(16384)->Arg(65536)->UseRealTime();

static void EventClientWriteHeavy(benchmark::State& state)
{
    BenchContext context;
    kiotty_test::LoopbackPeer peer;
    const uint16_t port = kiotty_test::unusedPort();
    if (!peer.listenTcp(port)) { state.SkipWithError("listen failed"); return; }
    BenchEventClient client(context.handle, port, Role::WORK, CHUNK_BYTES);
    if (!client.valid() || !peer.accept(WAIT_MS) || !waitFor(client.app.work_started, 1)) { state.SkipWithError("connect failed"); return; }
    const KiottyNetConnectionStream stream = client.stream();
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
BENCHMARK(EventClientWriteHeavy)->UseRealTime();

static void StreamClientReadHeavy(benchmark::State& state)
{
    BenchContext context;
    kiotty_test::LoopbackPeer peer;
    const uint16_t port = kiotty_test::unusedPort();
    if (!peer.listenTcp(port)) { state.SkipWithError("listen failed"); return; }
    const size_t capacity = static_cast<size_t>(state.range(0));
    BenchStreamClient client(context.handle, port, capacity);
    if (client.get() == nullptr) { state.SkipWithError("receive capacity above the compile time maximum or connect failed"); return; }
    if (!peer.accept(WAIT_MS)) { state.SkipWithError("connect failed"); return; }
    std::vector<char> payload(HEAVY_BYTES, 'r');
    std::vector<char> buffer(capacity);
    for (auto _ : state)
    {
        std::thread writer([&]() { (void)writeAll(peer, payload); });
        size_t got = 0;
        while (got < HEAVY_BYTES)
        {
            const KiottyNetResult read = KiottyNet_readStreamClient(client.get(), buffer.data(), buffer.size(), WAIT_MS);
            if (read.code != KIOTTY_NET_CODE_SUCCESS) { break; }
            got += static_cast<size_t>(read.bytes);
        }
        writer.join();
        if (got < HEAVY_BYTES) { state.SkipWithError("read failed"); break; }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(HEAVY_BYTES));
}
BENCHMARK(StreamClientReadHeavy)->ArgName("receive")->Arg(16384)->Arg(65536)->UseRealTime();

static void StreamClientWriteHeavy(benchmark::State& state)
{
    BenchContext context;
    kiotty_test::LoopbackPeer peer;
    const uint16_t port = kiotty_test::unusedPort();
    if (!peer.listenTcp(port)) { state.SkipWithError("listen failed"); return; }
    BenchStreamClient client(context.handle, port, CHUNK_BYTES);
    if (client.get() == nullptr || !peer.accept(WAIT_MS)) { state.SkipWithError("connect failed"); return; }
    std::vector<char> payload(HEAVY_BYTES, 'w');
    const KiottyNetSpan data = { payload.data(), payload.size() };
    for (auto _ : state)
    {
        std::thread reader([&]() { (void)readAll(peer, HEAVY_BYTES); });
        const KiottyNetResult written = KiottyNet_writeStreamClient(client.get(), data, WAIT_MS);
        reader.join();
        if (written.code != KIOTTY_NET_CODE_SUCCESS) { state.SkipWithError("write failed"); break; }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(HEAVY_BYTES));
}
BENCHMARK(StreamClientWriteHeavy)->UseRealTime();

#endif
