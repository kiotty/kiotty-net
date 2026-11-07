#include "data/repository/connection/kiotty_acceptor.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <vector>

using kiotty::net::implementation::Acceptor;
using kiotty::net::implementation::ActiveSocketResult;
using kiotty::net::implementation::EventListener;
using kiotty::net::implementation::IoAction;
using kiotty::net::implementation::IoActionKind;
using kiotty::net::implementation::IoCompletion;
using kiotty::net::implementation::IoKind;
using kiotty::net::implementation::IoPurpose;
using kiotty::net::implementation::NoDelay;
using kiotty::net::implementation::SocketCode;
using kiotty::net::implementation::Span;
using kiotty_test::LoopbackPeer;

TEST(Acceptor, UnopenedAcceptorIsNotListening)
{
    Acceptor acceptor;
    EXPECT_FALSE(acceptor.listening());
    EXPECT_FALSE(acceptor.isPending());
    EXPECT_EQ(acceptor.slot(), -1);
}

#if KIOTTY_NET_HAS_EVENT_BACKEND

namespace
{
    const uint32_t WAIT_MS = 2000;
    const size_t   CAPACITY = 8;
    const int      BACKLOG = 4;

    KiottyNetEndpoint loopback(uint16_t port)
    {
        KiottyNetEndpoint endpoint;
        std::memset(&endpoint, 0, sizeof(endpoint));
        endpoint.transport = KIOTTY_NET_TRANSPORT_TCP;
        kiotty_test::copyHost(endpoint.host, sizeof(endpoint.host), "127.0.0.1");
        endpoint.port = port;
        return endpoint;
    }

    std::vector<IoCompletion> collect(EventListener& listener, size_t count, uint32_t timeout_milliseconds)
    {
        std::vector<IoCompletion> collected;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        while (collected.size() < count && kiotty_test::elapsedMilliseconds(started) < timeout_milliseconds)
        {
            Span<const IoCompletion> waited = listener.wait(10);
            collected.insert(collected.end(), waited.begin(), waited.end());
        }
        return collected;
    }

    void submit(EventListener& listener, Acceptor& acceptor, const IoAction& action)
    {
        SocketCode submitted = listener.submit(acceptor.eventFor(action), action, acceptor.resolve(action));
        ASSERT_EQ(submitted, SocketCode::SUCCESS);
    }
}

TEST(Acceptor, OpenListensAndEventIsNotPendingUntilSubmitted)
{
    Acceptor acceptor;
    ASSERT_EQ(acceptor.open(loopback(0), BACKLOG, NoDelay::ENABLED), SocketCode::SUCCESS);
    EXPECT_TRUE(acceptor.listening());
    EXPECT_NE(acceptor.port(), 0);
    EXPECT_EQ(acceptor.slot(), -1);
    EXPECT_FALSE(acceptor.isPending());
    IoAction read = IoAction::read(IoPurpose::USER, 0, 1);
    EXPECT_EQ(&acceptor.eventFor(IoAction::accept()), &acceptor.eventFor(read));

    EventListener listener;
    ASSERT_EQ(listener.open(CAPACITY), SocketCode::SUCCESS);
    ASSERT_EQ(acceptor.attach(listener), SocketCode::SUCCESS);
    EXPECT_FALSE(acceptor.isPending());
    submit(listener, acceptor, IoAction::accept());
    EXPECT_TRUE(acceptor.isPending());
    acceptor.detach(listener);
    collect(listener, 1, WAIT_MS);
}

TEST(Acceptor, AcceptAfterCompletionReturnsPeerSocketWithNoDelayApplied)
{
    Acceptor acceptor;
    ASSERT_EQ(acceptor.open(loopback(0), BACKLOG, NoDelay::ENABLED), SocketCode::SUCCESS);
    EventListener listener;
    ASSERT_EQ(listener.open(CAPACITY), SocketCode::SUCCESS);
    ASSERT_EQ(acceptor.attach(listener), SocketCode::SUCCESS);
    submit(listener, acceptor, IoAction::accept());

    LoopbackPeer client;
    ASSERT_TRUE(client.connectTcp(acceptor.port()));
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::ACCEPT);
    EXPECT_EQ(completions[0].slot, -1);
    ASSERT_EQ(completions[0].code, SocketCode::SUCCESS);
    EXPECT_FALSE(acceptor.isPending());

    ActiveSocketResult accepted = acceptor.accept();
    ASSERT_TRUE(accepted) << static_cast<int>(accepted.code);
    EXPECT_TRUE(accepted.value.valid());
    EXPECT_TRUE(kiotty_test::noDelayOf(accepted.value.handle()));
    EXPECT_STREQ(accepted.value.peer().hostname, "127.0.0.1");
    EXPECT_NE(accepted.value.peer().port, 0);

    ASSERT_TRUE(client.sendAll("ping", 4));
    LoopbackPeer second;
    submit(listener, acceptor, IoAction::accept());
    ASSERT_TRUE(second.connectTcp(acceptor.port()));
    completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    ASSERT_EQ(completions[0].code, SocketCode::SUCCESS);
    ActiveSocketResult again = acceptor.accept();
    ASSERT_TRUE(again);
    EXPECT_NE(again.value.handle(), accepted.value.handle());

    acceptor.detach(listener);
}

TEST(Acceptor, DetachWhileAcceptPendingYieldsCancelledCompletion)
{
    Acceptor acceptor;
    ASSERT_EQ(acceptor.open(loopback(0), BACKLOG, NoDelay::DISABLED), SocketCode::SUCCESS);
    EventListener listener;
    ASSERT_EQ(listener.open(CAPACITY), SocketCode::SUCCESS);
    ASSERT_EQ(acceptor.attach(listener), SocketCode::SUCCESS);
    submit(listener, acceptor, IoAction::accept());
    ASSERT_TRUE(acceptor.isPending());

    acceptor.detach(listener);
    acceptor.shutdown();
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::ACCEPT);
    EXPECT_EQ(completions[0].code, SocketCode::CANCELLED);
    EXPECT_EQ(completions[0].action.kind, IoActionKind::ACCEPT);
    EXPECT_FALSE(acceptor.isPending());
    EXPECT_FALSE(acceptor.listening());
}

#endif
