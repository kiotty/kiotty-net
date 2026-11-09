#include "domain/usecases/io_loop/kiotty_io_loop.h"

#include "core/pool/kiotty_reusable_pool.h"
#include "data/repository/connection/kiotty_acceptor.h"
#include "data/repository/connection/kiotty_connection.h"
#include "data/repository/connection/kiotty_connection_host.h"
#include "data/repository/connection/kiotty_connection_status.h"
#include "data/repository/connection/kiotty_notification_desk.h"
#include "support/kiotty_test_handler.h"
#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_text.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if KIOTTY_NET_HAS_EVENT_BACKEND

namespace net = kiotty::net::implementation;
using kiotty_test::LoopbackPeer;

namespace
{
    const uint32_t WAIT_MS = 2000;
    const uint32_t WATCHDOG_MS = 5000;
    const int      BACKLOG = 4;
    const size_t   CAPACITY = 16;
    const size_t   POOL_SLOTS = 4;
    const size_t   STAGE_BYTES = 1024;
    const size_t   MESSAGE_BYTES = 1024;
    const size_t   NOTICE_BYTES = 6;
    const char*    LOOPBACK_HOST = "127.0.0.1";

    using Pool = net::ReusablePool<net::Connection, POOL_SLOTS>;

    enum class Mark : uint8_t
    {
        CONNECTED,
        READY,
        READ,
        WRITTEN,
        TIMEOUT,
        WORK_STARTED,
        DISCONNECTED,
        REPORTED_ERROR
    };

    struct Probe
    {
        Probe();

        int count(Mark mark) const;
        bool timeoutAfterDisconnect() const;

        std::vector<Mark>         marks;
        std::string               received;
        std::string               outgoing;
        net::ConnectionHost*      host;
        net::NotificationDesk*    desk;
        net::IoLoop*              loop;
        std::atomic<int64_t>      connection_id;
        std::atomic<int32_t>      ready_count;
        std::atomic<int32_t>      work_started_count;
        std::atomic<int32_t>      disconnect_count;
        KiottyNetCode             disconnect_reason;
        KiottyNetCode             last_error;
        KiottyNetCode             notify_code;
        uint8_t                   pending_at_work_start;
        bool                      io_thread_in_callback;

        KiottyNetConnectionAction (*on_ready)(Probe& probe);
        KiottyNetConnectionAction (*on_read)(Probe& probe, KiottyNetSpan data);
        KiottyNetConnectionAction (*on_timeout)(Probe& probe);
        KiottyNetConnectionAction (*on_written)(Probe& probe);
        void                      (*on_work_started)(Probe& probe);
    };

    Probe::Probe()
        : marks()
        , received()
        , outgoing()
        , host(nullptr)
        , desk(nullptr)
        , loop(nullptr)
        , connection_id(0)
        , ready_count(0)
        , work_started_count(0)
        , disconnect_count(0)
        , disconnect_reason(KIOTTY_NET_CODE_SUCCESS)
        , last_error(KIOTTY_NET_CODE_SUCCESS)
        , notify_code(KIOTTY_NET_CODE_SUCCESS)
        , pending_at_work_start(0)
        , io_thread_in_callback(false)
        , on_ready(nullptr)
        , on_read(nullptr)
        , on_timeout(nullptr)
        , on_written(nullptr)
        , on_work_started(nullptr)
    {
    }

    int Probe::count(Mark mark) const
    {
        int total = 0;
        for (Mark current : marks)
        {
            if (current == mark)
            {
                total += 1;
            }
        }
        return total;
    }

    bool Probe::timeoutAfterDisconnect() const
    {
        bool disconnected = false;
        for (Mark current : marks)
        {
            if (current == Mark::DISCONNECTED)
            {
                disconnected = true;
            }
            if (disconnected && current == Mark::TIMEOUT)
            {
                return true;
            }
        }
        return false;
    }

    Probe& probeOf(void* context)
    {
        return *static_cast<Probe*>(context);
    }

    KiottyNetConnectionDecision onConnected(KiottyNetAppContext app_context, KiottyNetConnectionInfo info)
    {
        (void)info;
        Probe& probe = probeOf(app_context);
        probe.marks.push_back(Mark::CONNECTED);
        KiottyNetConnectionDecision decision = { true, app_context };
        return decision;
    }

    KiottyNetConnectionAction onReady(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        Probe& probe = probeOf(context);
        probe.marks.push_back(Mark::READY);
        probe.connection_id.store(connection.id);
        probe.io_thread_in_callback = probe.loop != nullptr && probe.loop->isIoThread();
        probe.ready_count.fetch_add(1);
        if (probe.on_ready == nullptr)
        {
            return kiotty_test::thenHold(0);
        }
        return probe.on_ready(probe);
    }

    KiottyNetConnectionAction onRead(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetSpan data)
    {
        (void)connection;
        Probe& probe = probeOf(context);
        probe.marks.push_back(Mark::READ);
        probe.received.append(data.data, data.length);
        if (probe.on_read == nullptr)
        {
            return kiotty_test::thenHold(0);
        }
        return probe.on_read(probe, data);
    }

    KiottyNetConnectionAction onWritten(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, size_t length)
    {
        (void)connection;
        (void)length;
        Probe& probe = probeOf(context);
        probe.marks.push_back(Mark::WRITTEN);
        if (probe.on_written == nullptr)
        {
            return kiotty_test::thenHold(0);
        }
        return probe.on_written(probe);
    }

    KiottyNetConnectionAction onTimeout(KiottyNetConnectionContext context, KiottyNetConnectionHandle connection)
    {
        (void)connection;
        Probe& probe = probeOf(context);
        probe.marks.push_back(Mark::TIMEOUT);
        if (probe.on_timeout == nullptr)
        {
            return kiotty_test::thenHold(0);
        }
        return probe.on_timeout(probe);
    }

    void onWorkStarted(KiottyNetConnectionContext context, KiottyNetConnectionStream stream)
    {
        Probe& probe = probeOf(context);
        probe.marks.push_back(Mark::WORK_STARTED);
        const net::PoolHandle id = net::PoolHandle::fromValue(stream.id);
        probe.pending_at_work_start = probe.host->at(id.index).status().pending;
        probe.work_started_count.fetch_add(1);
        if (probe.on_work_started != nullptr)
        {
            probe.on_work_started(probe);
        }
    }

    void onDisconnected(KiottyNetAppContext app_context, KiottyNetConnectionContext context, KiottyNetConnectionHandle connection, KiottyNetCode reason)
    {
        (void)context;
        (void)connection;
        Probe& probe = probeOf(app_context);
        probe.marks.push_back(Mark::DISCONNECTED);
        probe.disconnect_reason = reason;
        probe.disconnect_count.fetch_add(1);
    }

    void onError(KiottyNetAppContext app_context, KiottyNetConnectionInfo info, KiottyNetCode code, const char* message)
    {
        (void)info;
        (void)message;
        Probe& probe = probeOf(app_context);
        probe.marks.push_back(Mark::REPORTED_ERROR);
        probe.last_error = code;
    }

    KiottyNetConnectionHandler makeCallbacks()
    {
        KiottyNetConnectionHandler table;
        table.onConnected = &onConnected;
        table.onReady = &onReady;
        table.onRead = &onRead;
        table.onWritten = &onWritten;
        table.onTimeout = &onTimeout;
        table.onWorkStarted = &onWorkStarted;
        table.onDisconnected = &onDisconnected;
        table.onError = &onError;
        return table;
    }

    KiottyNetAddress loopbackAddress(uint16_t port)
    {
        KiottyNetAddress address;
        std::memset(&address, 0, sizeof(address));
        address.transport = KIOTTY_NET_TRANSPORT_TCP;
        kiotty_test::copyHost(address.host, sizeof(address.host), LOOPBACK_HOST);
        address.port = port;
        return address;
    }

    class Watchdog
    {
    public:
        Watchdog(net::IoLoop& loop, net::EventListener& listener);
        ~Watchdog();

        Watchdog(const Watchdog&) = delete;
        Watchdog& operator=(const Watchdog&) = delete;

        void finish();
        bool fired() const;

    private:
        void watch();

        net::IoLoop&         _loop;
        net::EventListener&  _listener;
        std::atomic<bool>    _finished;
        std::atomic<bool>    _fired;
        std::thread          _thread;
    };

    Watchdog::Watchdog(net::IoLoop& loop, net::EventListener& listener)
        : _loop(loop)
        , _listener(listener)
        , _finished(false)
        , _fired(false)
        , _thread()
    {
        _thread = std::thread([this]() { watch(); });
    }

    Watchdog::~Watchdog()
    {
        finish();
    }

    void Watchdog::finish()
    {
        _finished.store(true);
        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    bool Watchdog::fired() const
    {
        return _fired.load();
    }

    void Watchdog::watch()
    {
        const bool in_time = kiotty_test::waitUntil([this]() { return _finished.load(); }, WATCHDOG_MS);
        if (in_time)
        {
            return;
        }
        _fired.store(true);
        _loop.stop();
        (void)_listener.wake();
    }

    class Rig
    {
    public:
        explicit Rig(bool listen);

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        net::LoopParts parts();
        bool           run(net::StopPolicy policy);
        KiottyNetCode  closeStreamOf(int64_t connection_id);

        bool opened() const;

        std::unique_ptr<Pool>               pool;
        net::ConnectionHost                 host;
        net::NotificationDesk               desk;
        net::EventListener                  listener;
        net::Acceptor                       acceptor;
        net::TlsProfile                     profile;
        kiotty_test::FakeEndpoint              owner;
        Probe                               probe;
        const KiottyNetConnectionHandler    callbacks;
        net::Handler                        handler;
        net::IoLoop                         loop;

    private:
        bool _opened;
    };

    Rig::Rig(bool listen)
        : pool(new Pool())
        , host(*pool)
        , desk(*pool, host.notificationMutex())
        , listener()
        , acceptor()
        , profile()
        , owner()
        , probe()
        , callbacks(makeCallbacks())
        , handler(callbacks, &probe, owner)
        , loop()
        , _opened(false)
    {
        const net::ConnectionLimits limits{ STAGE_BYTES, STAGE_BYTES, STAGE_BYTES };
        host.initialize(limits, POOL_SLOTS, net::TlsCapacity{ STAGE_BYTES, STAGE_BYTES });
        probe.host = &host;
        probe.desk = &desk;
        probe.loop = &loop;
        bool ready = listener.open(CAPACITY) == net::SocketCode::SUCCESS;
        if (listen)
        {
            ready = ready && acceptor.open(loopbackAddress(0), BACKLOG, net::NoDelay::ENABLED) == net::SocketCode::SUCCESS;
            ready = ready && acceptor.attach(listener) == net::SocketCode::SUCCESS;
        }
        _opened = ready;
    }

    bool Rig::opened() const
    {
        return _opened;
    }

    net::LoopParts Rig::parts()
    {
        return net::LoopParts{ host, listener, acceptor, profile, handler };
    }

    bool Rig::run(net::StopPolicy policy)
    {
        Watchdog guard(loop, listener);
        const KiottyNetCode code = loop.run(parts(), policy);
        guard.finish();
        return !guard.fired() && code == KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode Rig::closeStreamOf(int64_t connection_id)
    {
        return desk.closeStream(net::PoolHandle::fromValue(connection_id),
                                kiotty_test::thenClose(KIOTTY_NET_CODE_CLOSED));
    }

    bool connectedTo(LoopbackPeer& peer, uint16_t port)
    {
        return peer.connectTcp(port);
    }

    bool waitForReady(Probe& probe)
    {
        return kiotty_test::waitUntil([&probe]() { return probe.ready_count.load() > 0; }, WAIT_MS);
    }

    bool waitForWorkStart(Probe& probe)
    {
        return kiotty_test::waitUntil([&probe]() { return probe.work_started_count.load() > 0; }, WAIT_MS);
    }

    bool waitForDisconnect(Probe& probe)
    {
        return kiotty_test::waitUntil([&probe]() { return probe.disconnect_count.load() > 0; }, WAIT_MS);
    }

    bool sameBytes(const char* expected, const char* actual, size_t length)
    {
        return std::memcmp(expected, actual, length) == 0;
    }
}

TEST(IoLoop, LoopbackEchoRoundTripPlain)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenRead(4, 0); };
    rig.probe.on_read = [](Probe& probe, KiottyNetSpan data) {
        probe.outgoing.assign(data.data, data.length);
        return kiotty_test::thenWriteSpan(kiotty_test::spanOf(probe.outgoing), 0);
    };
    rig.probe.on_written = [](Probe&) { return kiotty_test::thenRead(4, 0); };

    char reply[4] = { 0, 0, 0, 0 };
    bool sent = false;
    bool received = false;
    std::thread client([&]() {
        LoopbackPeer peer;
        sent = connectedTo(peer, rig.acceptor.port()) && peer.sendAll("ping", 4);
        received = sent && peer.recvExactly(reply, 4, WAIT_MS);
        rig.loop.stop();
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_TRUE(sent);
    EXPECT_TRUE(received);
    EXPECT_TRUE(sameBytes("ping", reply, 4));
    EXPECT_EQ(rig.probe.count(Mark::CONNECTED), 1);
    EXPECT_EQ(rig.probe.count(Mark::READ), 1);
    EXPECT_EQ(rig.probe.count(Mark::WRITTEN), 1);
    EXPECT_EQ(rig.probe.disconnect_count.load(), 1);
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, TimeoutIsNotDeliveredAfterCloseInTheSameBatch)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenRead(4, 1); };
    rig.probe.on_timeout = [](Probe&) { return kiotty_test::thenRead(4, 1); };

    std::thread client([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        peer.close();
        (void)waitForDisconnect(rig.probe);
        rig.loop.stop();
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_GT(rig.probe.count(Mark::TIMEOUT), 0);
    EXPECT_EQ(rig.probe.disconnect_count.load(), 1);
    EXPECT_FALSE(rig.probe.timeoutAfterDisconnect());
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, WorkStartedFiresInTheSameRunOnceWhenWriteLaneIsIdle)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenWork(); };

    std::thread client([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()) || !waitForReady(rig.probe))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        (void)waitForWorkStart(rig.probe);
        rig.loop.stop();
        (void)rig.listener.wake();
        (void)rig.closeStreamOf(rig.probe.connection_id.load());
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_EQ(rig.probe.work_started_count.load(), 1);
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, WorkStartedWaitsForInFlightNotifyThenFires)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe& probe) {
        const std::string notice = "notice";
        probe.notify_code = probe.desk->notify(net::PoolHandle::fromValue(probe.connection_id.load()),
                                               kiotty_test::spanOf(notice));
        return kiotty_test::thenRead(1, 0);
    };
    rig.probe.on_read = [](Probe&, KiottyNetSpan) { return kiotty_test::thenWork(); };

    char notice_seen[NOTICE_BYTES] = { 0 };
    bool notice_received = false;
    std::thread client([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()) || !waitForReady(rig.probe))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        notice_received = peer.recvExactly(notice_seen, NOTICE_BYTES, WAIT_MS);
        (void)peer.sendAll("x", 1);
        (void)waitForWorkStart(rig.probe);
        rig.loop.stop();
        (void)rig.listener.wake();
        (void)rig.closeStreamOf(rig.probe.connection_id.load());
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_EQ(rig.probe.notify_code, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_TRUE(notice_received);
    EXPECT_TRUE(sameBytes("notice", notice_seen, NOTICE_BYTES));
    EXPECT_EQ(rig.probe.work_started_count.load(), 1);
    EXPECT_EQ(rig.probe.pending_at_work_start & net::PENDING_WRITING, 0);
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, WorkRequestedByATimeoutHandlerStartsInTheSameRunOnce)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenHold(20); };
    rig.probe.on_timeout = [](Probe&) { return kiotty_test::thenWork(); };

    std::thread client([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        (void)waitForWorkStart(rig.probe);
        rig.loop.stop();
        (void)rig.listener.wake();
        (void)rig.closeStreamOf(rig.probe.connection_id.load());
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_EQ(rig.probe.work_started_count.load(), 1);
    EXPECT_GT(rig.probe.count(Mark::TIMEOUT), 0);
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, NotifyDuringUserWriteArrivesAsTwoWholeMessagesInOrder)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe& probe) {
        probe.outgoing.assign(MESSAGE_BYTES, 'a');
        return kiotty_test::thenWriteSpan(kiotty_test::spanOf(probe.outgoing), 0);
    };

    const std::string message(MESSAGE_BYTES, 'a');
    const std::string notice(NOTICE_BYTES, 'n');
    std::vector<char> first(MESSAGE_BYTES, 0);
    std::vector<char> second(NOTICE_BYTES, 0);
    bool first_received = false;
    bool second_received = false;
    KiottyNetCode staged = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    std::thread client([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()) || !waitForReady(rig.probe))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        first_received = peer.recvExactly(first.data(), MESSAGE_BYTES, WAIT_MS);
        staged = rig.desk.notify(net::PoolHandle::fromValue(rig.probe.connection_id.load()),
                                 kiotty_test::spanOf(notice));
        (void)rig.listener.wake();
        second_received = peer.recvExactly(second.data(), NOTICE_BYTES, WAIT_MS);
        rig.loop.stop();
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_EQ(staged, KIOTTY_NET_CODE_SUCCESS);
    EXPECT_TRUE(first_received);
    EXPECT_TRUE(second_received);
    EXPECT_TRUE(sameBytes(message.data(), first.data(), MESSAGE_BYTES));
    EXPECT_TRUE(sameBytes(notice.data(), second.data(), NOTICE_BYTES));
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, EndpointCloseDuringWorkShutsDownSocketAndReleasesAfterCloseStream)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenWork(); };

    bool saw_end_of_stream = false;
    bool held_while_closing = false;
    KiottyNetCode close_code = KIOTTY_NET_CODE_SUCCESS;
    std::thread worker([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()) || !waitForWorkStart(rig.probe))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        rig.loop.stop();
        (void)rig.listener.wake();
        char byte = 0;
        saw_end_of_stream = !peer.recvExactly(&byte, 1, WAIT_MS);
        const net::PoolHandle id = net::PoolHandle::fromValue(rig.probe.connection_id.load());
        const net::ConnectionStatus status = rig.host.at(id.index).status();
        held_while_closing = status.phase == net::Phase::CLOSING
                             && (status.pending & net::PENDING_WORKING) != 0;
        close_code = rig.closeStreamOf(rig.probe.connection_id.load());
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    worker.join();
    EXPECT_TRUE(finished);
    EXPECT_TRUE(saw_end_of_stream);
    EXPECT_TRUE(held_while_closing);
    EXPECT_EQ(close_code, KIOTTY_NET_CODE_CLOSED);
    EXPECT_EQ(rig.probe.disconnect_count.load(), 1);
    EXPECT_EQ(rig.probe.disconnect_reason, KIOTTY_NET_CODE_CLOSED);
    EXPECT_TRUE(rig.host.empty());
}

TEST(IoLoop, IsIoThreadIsTrueOnlyOnTheRunningThread)
{
    Rig rig(true);
    ASSERT_TRUE(rig.opened());
    EXPECT_FALSE(rig.loop.isIoThread());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenHold(0); };

    bool peer_sees_io_thread = true;
    std::thread client([&]() {
        LoopbackPeer peer;
        if (!connectedTo(peer, rig.acceptor.port()) || !waitForReady(rig.probe))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        peer_sees_io_thread = rig.loop.isIoThread();
        rig.loop.stop();
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_STOPPED);
    client.join();
    EXPECT_TRUE(finished);
    EXPECT_TRUE(rig.probe.io_thread_in_callback);
    EXPECT_FALSE(peer_sees_io_thread);
    EXPECT_FALSE(rig.loop.isIoThread());
}

TEST(IoLoop, AdmittedConnectionStartsWorkBeforeTheFirstWait)
{
    Rig rig(false);
    ASSERT_TRUE(rig.opened());
    rig.probe.on_ready = [](Probe&) { return kiotty_test::thenWork(); };

    LoopbackPeer server;
    ASSERT_TRUE(server.listenTcp(0));
    net::ActiveSocketResult opened = net::ActiveSocket::open(KIOTTY_NET_TRANSPORT_TCP);
    ASSERT_TRUE(opened);
    ASSERT_EQ(opened.value.connect(loopbackAddress(server.port()), WAIT_MS), net::SocketCode::SUCCESS);
    rig.loop.admit(rig.parts(), std::move(opened.value), kiotty_test::textOf(LOOPBACK_HOST));

    std::thread worker([&]() {
        if (!waitForWorkStart(rig.probe))
        {
            rig.loop.stop();
            (void)rig.listener.wake();
            return;
        }
        (void)rig.closeStreamOf(rig.probe.connection_id.load());
        (void)rig.listener.wake();
    });

    const bool finished = rig.run(net::StopPolicy::WHEN_EMPTY);
    worker.join();
    EXPECT_TRUE(finished);
    EXPECT_EQ(rig.probe.work_started_count.load(), 1);
    EXPECT_EQ(rig.probe.disconnect_count.load(), 1);
    EXPECT_TRUE(rig.host.empty());
}

#endif
