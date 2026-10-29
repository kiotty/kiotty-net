#include "base/event/kiotty_event_listener.h"
#include "base/socket/kiotty_socket.h"

#include "support/kiotty_test_peer.h"
#include "support/kiotty_test_wait.h"

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <mswsock.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using kiotty::Holder;
using kiotty::Span;
using kiotty::net::AcceptBuffer;
using kiotty::net::ActiveSocket;
using kiotty::net::ActiveSocketResult;
using kiotty::net::AttachResult;
using kiotty::net::IoAction;
using kiotty::net::IoActionKind;
using kiotty::net::IoCompletion;
using kiotty::net::IoEvent;
using kiotty::net::IoKind;
using kiotty::net::IoPayload;
using kiotty::net::IoPurpose;
using kiotty::net::PassiveSocket;
using kiotty::net::SocketCode;
using kiotty_test::LoopbackPeer;

namespace net = kiotty::net;

namespace
{
    const uint32_t WAIT_MS = 2000;
    const size_t   CAPACITY = 16;
    const int32_t  SLOT = 7;

    KiottyNetEndpoint loopback(uint16_t port)
    {
        KiottyNetEndpoint endpoint;
        std::memset(&endpoint, 0, sizeof(endpoint));
        endpoint.transport = KIOTTY_TCP;
        kiotty_test::copyHost(endpoint.host, sizeof(endpoint.host), "127.0.0.1");
        endpoint.port = port;
        return endpoint;
    }

    IoPayload payloadOf(void* buffer, size_t length)
    {
        IoPayload payload = { buffer, length };
        return payload;
    }

    struct Link
    {
        LoopbackPeer peer;
        ActiveSocket socket;
    };

    bool connect(Link& link)
    {
        if (!link.peer.listenTcp(0))
        {
            return false;
        }
        ActiveSocketResult opened = ActiveSocket::open(KIOTTY_TCP);
        if (!opened.isOk())
        {
            return false;
        }
        link.socket = std::move(opened.value());
        if (link.socket.connect(loopback(link.peer.port()), WAIT_MS) != SocketCode::SUCCESS)
        {
            return false;
        }
        return link.peer.accept(WAIT_MS);
    }

    Holder<IoEvent> attach(net::EventListener& listener, kiotty::net::SocketHandle socket, int32_t slot)
    {
        AttachResult attached = listener.attach(socket, slot);
        EXPECT_TRUE(attached.isOk()) << static_cast<int>(attached.code());
        if (!attached.isOk())
        {
            return Holder<IoEvent>();
        }
        return std::move(attached.value());
    }

    std::vector<IoCompletion> collect(net::EventListener& listener, size_t count, uint32_t timeout_ms)
    {
        std::vector<IoCompletion> collected;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        while (collected.size() < count && kiotty_test::elapsedMs(started) < timeout_ms)
        {
            Span<const IoCompletion> waited = listener.wait(10);
            collected.insert(collected.end(), waited.begin(), waited.end());
        }
        return collected;
    }

    std::vector<char> pattern(size_t length)
    {
        std::vector<char> bytes(length);
        for (size_t i = 0; i < length; ++i)
        {
            bytes[i] = static_cast<char>('a' + i % 26);
        }
        return bytes;
    }

    class EventListener : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ASSERT_EQ(listener.open(CAPACITY), SocketCode::SUCCESS);
            ASSERT_TRUE(connect(link));
            stream = attach(listener, link.socket.handle(), SLOT);
            ASSERT_TRUE(static_cast<bool>(stream));
        }

        void TearDown() override
        {
            listener.detach(stream);
            link.socket.close();
            collect(listener, 1, 50);
        }

        net::EventListener listener;
        Link               link;
        Holder<IoEvent>    stream;
    };

    class AcceptTarget
    {
    public:
        AcceptTarget()
        {
            std::memset(&_buffer, 0, sizeof(_buffer));
            _buffer.socket = kiotty::net::INVALID_SOCKET_HANDLE;
#if defined(_WIN32)
            ActiveSocketResult opened = ActiveSocket::open(KIOTTY_TCP);
            EXPECT_TRUE(opened.isOk());
            if (opened.isOk())
            {
                _socket = std::move(opened.value());
                _buffer.socket = _socket.handle();
            }
#endif
        }

        IoPayload payload()
        {
            return payloadOf(&_buffer, sizeof(_buffer));
        }

        AcceptBuffer& buffer()
        {
            return _buffer;
        }

    private:
        ActiveSocket _socket;
        AcceptBuffer _buffer;
    };
}

TEST(IoEvent, EventFromAttachIsNotPending)
{
    net::EventListener listener;
    ASSERT_EQ(listener.open(CAPACITY), SocketCode::SUCCESS);
    Link link;
    ASSERT_TRUE(connect(link));
    Holder<IoEvent> event = attach(listener, link.socket.handle(), SLOT);
    ASSERT_TRUE(static_cast<bool>(event));
    EXPECT_FALSE(event->pending());
    EXPECT_EQ(event->action().kind, IoActionKind::NONE);
}

TEST_F(EventListener, SubmitReadThenPeerSendCompletesWithLengthAndTheSameAction)
{
    char buffer[64];
    IoAction read = IoAction::read(IoPurpose::USER, 123, 5);
    ASSERT_EQ(listener.submit(stream, read, payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    ASSERT_TRUE(link.peer.sendAll("hello", 5));
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    const IoCompletion& completion = completions[0];
    EXPECT_EQ(completion.kind, IoKind::RECEIVE);
    EXPECT_EQ(completion.slot, SLOT);
    EXPECT_EQ(completion.code, SocketCode::SUCCESS);
    EXPECT_EQ(completion.length, 5u);
    EXPECT_EQ(std::memcmp(buffer, "hello", 5), 0);
    EXPECT_EQ(completion.action.kind, IoActionKind::READ);
    EXPECT_EQ(completion.action.purpose, IoPurpose::USER);
    EXPECT_EQ(completion.action.timeout_ms, 123u);
    EXPECT_EQ(completion.action.as.read.exact, 5u);
}

TEST_F(EventListener, SubmitWriteCompletesWithFullLengthAndPeerReceivesBytes)
{
    std::vector<char> sent = pattern(4096);
    IoAction write = IoAction::write(IoPurpose::USER, 0);
    ASSERT_EQ(listener.submit(stream, write, payloadOf(sent.data(), sent.size())), SocketCode::SUCCESS);
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::SEND);
    EXPECT_EQ(completions[0].code, SocketCode::SUCCESS);
    EXPECT_EQ(completions[0].length, sent.size());
    std::vector<char> received(sent.size());
    ASSERT_TRUE(link.peer.recvExactly(received.data(), received.size(), WAIT_MS));
    EXPECT_EQ(received, sent);
}

TEST_F(EventListener, PendingIsTrueAfterSubmitAndFalseAfterWaitReturnsIt)
{
    char buffer[16];
    EXPECT_FALSE(stream->pending());
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    EXPECT_TRUE(stream->pending());
    ASSERT_TRUE(link.peer.sendAll("x", 1));
    ASSERT_TRUE(link.peer.sendAll("y", 1));
    EXPECT_TRUE(stream->pending());
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_FALSE(stream->pending());
}

TEST_F(EventListener, WaitWithTimeoutAndNothingPendingReturnsEmptySpanAfterTimeout)
{
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Span<const IoCompletion> waited = listener.wait(50);
    EXPECT_TRUE(waited.empty());
    EXPECT_GE(kiotty_test::elapsedMs(started), 50u);
}

TEST_F(EventListener, WaitWithZeroTimeoutReturnsImmediately)
{
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Span<const IoCompletion> waited = listener.wait(0);
    EXPECT_TRUE(waited.empty());
    EXPECT_LT(kiotty_test::elapsedMs(started), 50u);
}

TEST_F(EventListener, WakeFromAnotherThreadReturnsOneWakeCompletion)
{
    SocketCode woken = SocketCode::UNKNOWN;
    std::thread waker([&]() {
        woken = listener.wake();
    });
    Span<const IoCompletion> waited = listener.wait(net::EventListener::WAIT_FOREVER);
    waker.join();
    EXPECT_EQ(woken, SocketCode::SUCCESS);
    ASSERT_EQ(waited.length, 1u);
    EXPECT_EQ(waited[0].kind, IoKind::WAKE);
    EXPECT_EQ(waited[0].slot, -1);
}

TEST_F(EventListener, TwoWakesBeforeWaitAreDeliveredAsAtMostTwoWakeCompletions)
{
    ASSERT_EQ(listener.wake(), SocketCode::SUCCESS);
    ASSERT_EQ(listener.wake(), SocketCode::SUCCESS);
    std::vector<IoCompletion> completions;
    Span<const IoCompletion> first = listener.wait(WAIT_MS);
    completions.insert(completions.end(), first.begin(), first.end());
    Span<const IoCompletion> rest = listener.wait(0);
    completions.insert(completions.end(), rest.begin(), rest.end());
    EXPECT_GE(completions.size(), 1u);
    EXPECT_LE(completions.size(), 2u);
    for (const IoCompletion& completion : completions)
    {
        EXPECT_EQ(completion.kind, IoKind::WAKE);
        EXPECT_EQ(completion.slot, -1);
    }
}

TEST_F(EventListener, PeerCloseCompletesPendingReadWithClosedAndZeroLength)
{
    char buffer[16];
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    ASSERT_TRUE(link.peer.shutdownWrite());
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::RECEIVE);
    EXPECT_EQ(completions[0].code, SocketCode::CLOSED);
    EXPECT_EQ(completions[0].length, 0u);
}

TEST_F(EventListener, PeerResetCompletesPendingReadWithResetCode)
{
    char buffer[16];
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    ASSERT_TRUE(link.peer.reset());
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::RECEIVE);
    EXPECT_EQ(completions[0].code, SocketCode::RESET);
}

TEST_F(EventListener, DetachWithPendingReadDeliversCancelledCompletionBeforeNextWaitReturns)
{
    char buffer[16];
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    listener.detach(stream);
    link.socket.close();
    Span<const IoCompletion> waited = listener.wait(WAIT_MS);
    ASSERT_EQ(waited.length, 1u);
    EXPECT_EQ(waited[0].kind, IoKind::RECEIVE);
    EXPECT_EQ(waited[0].slot, SLOT);
    EXPECT_EQ(waited[0].code, SocketCode::CANCELLED);
    EXPECT_FALSE(stream->pending());
}

TEST_F(EventListener, DetachWithPendingReadAndWriteDeliversTwoCancelled)
{
    Holder<IoEvent> notify = attach(listener, link.socket.handle(), SLOT);
    ASSERT_TRUE(static_cast<bool>(notify));
    ASSERT_TRUE(kiotty_test::setSendBuffer(link.socket.handle(), 0));
    char buffer[16];
    std::vector<char> flood = pattern(64 * 1024 * 1024);
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    ASSERT_EQ(listener.submit(notify, IoAction::write(IoPurpose::NOTIFY, 0), payloadOf(flood.data(), flood.size())), SocketCode::SUCCESS);
    EXPECT_TRUE(listener.wait(50).empty());
    listener.detach(stream);
    listener.detach(notify);
    link.socket.close();
    std::vector<IoCompletion> completions = collect(listener, 2, WAIT_MS);
    ASSERT_EQ(completions.size(), 2u);
    for (const IoCompletion& completion : completions)
    {
        EXPECT_EQ(completion.code, SocketCode::CANCELLED) << static_cast<int>(completion.kind);
    }
    EXPECT_FALSE(stream->pending());
    EXPECT_FALSE(notify->pending());
}

TEST_F(EventListener, DetachWithNothingPendingDeliversNothing)
{
    listener.detach(stream);
    link.socket.close();
    EXPECT_TRUE(listener.wait(50).empty());
}

TEST_F(EventListener, TwoSendsSubmittedInOrderArriveAtPeerInThatOrder)
{
    Holder<IoEvent> notify = attach(listener, link.socket.handle(), SLOT);
    ASSERT_TRUE(static_cast<bool>(notify));
    char user[] = "user-bytes|";
    char pushed[] = "notify-bytes";
    ASSERT_EQ(listener.submit(stream, IoAction::write(IoPurpose::USER, 0), payloadOf(user, sizeof(user) - 1)), SocketCode::SUCCESS);
    ASSERT_EQ(listener.submit(notify, IoAction::write(IoPurpose::NOTIFY, 0), payloadOf(pushed, sizeof(pushed) - 1)), SocketCode::SUCCESS);
    std::vector<IoCompletion> completions = collect(listener, 2, WAIT_MS);
    ASSERT_EQ(completions.size(), 2u);
    std::string expected = std::string(user) + pushed;
    std::vector<char> received(expected.size());
    ASSERT_TRUE(link.peer.recvExactly(received.data(), received.size(), WAIT_MS));
    EXPECT_EQ(std::string(received.begin(), received.end()), expected);
}

TEST_F(EventListener, WriteLargerThanSocketBufferCompletesPossiblyPartiallyAndTheRestIsResubmittable)
{
    const size_t total = 1024 * 1024;
    std::vector<char> sent = pattern(total);
    std::vector<char> received(total);
    bool all_received = false;
    std::thread reader([&]() {
        size_t done = 0;
        const size_t chunk = 16 * 1024;
        while (done < total)
        {
            size_t length = std::min(chunk, total - done);
            if (!link.peer.recvExactly(received.data() + done, length, WAIT_MS))
            {
                return;
            }
            done += length;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        all_received = true;
    });
    size_t offset = 0;
    while (offset < total)
    {
        IoPayload rest = payloadOf(sent.data() + offset, total - offset);
        ASSERT_EQ(listener.submit(stream, IoAction::write(IoPurpose::USER, 0), rest), SocketCode::SUCCESS);
        std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS * 5);
        ASSERT_EQ(completions.size(), 1u);
        ASSERT_EQ(completions[0].code, SocketCode::SUCCESS);
        ASSERT_GT(completions[0].length, 0u);
        ASSERT_LE(completions[0].length, total - offset);
        offset += completions[0].length;
    }
    reader.join();
    EXPECT_TRUE(all_received);
    EXPECT_EQ(received, sent);
}

TEST_F(EventListener, AcceptSubmitCompletesWhenAClientConnects)
{
    PassiveSocket passive;
    ASSERT_EQ(passive.open(loopback(0), 4), SocketCode::SUCCESS);
    Holder<IoEvent> event = attach(listener, passive.handle(), -1);
    ASSERT_TRUE(static_cast<bool>(event));
    AcceptTarget target;
    ASSERT_EQ(listener.submit(event, IoAction::accept(), target.payload()), SocketCode::SUCCESS);
    LoopbackPeer client;
    ASSERT_TRUE(client.connectTcp(passive.port()));
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::ACCEPT);
    EXPECT_EQ(completions[0].slot, -1);
    EXPECT_EQ(completions[0].code, SocketCode::SUCCESS);
    EXPECT_EQ(completions[0].action.kind, IoActionKind::ACCEPT);
}

TEST_F(EventListener, ImmediateFailureIsDeliveredAsAnErrorCompletionNotAReturnCode)
{
    char buffer[16];
    link.socket.close();
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].kind, IoKind::RECEIVE);
    EXPECT_NE(completions[0].code, SocketCode::SUCCESS);
    EXPECT_FALSE(stream->pending());
}

TEST_F(EventListener, SubmitOnEventThatIsAlreadyPendingIsRejectedWithBusy)
{
    char buffer[16];
    ASSERT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 10, 5), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    EXPECT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 20, 9), payloadOf(buffer, sizeof(buffer))), SocketCode::BUSY);
    ASSERT_TRUE(link.peer.sendAll("abc", 3));
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    EXPECT_EQ(completions[0].action.timeout_ms, 10u);
    EXPECT_EQ(completions[0].action.as.read.exact, 5u);
}

TEST_F(EventListener, SubmitOnEmptyOrDetachedEventIsRejectedWithInvalidArgument)
{
    char buffer[16];
    Holder<IoEvent> empty;
    EXPECT_EQ(listener.submit(empty, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::INVALID_ARGUMENT);
    listener.detach(stream);
    EXPECT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::INVALID_ARGUMENT);
    net::EventListener other;
    ASSERT_EQ(other.open(CAPACITY), SocketCode::SUCCESS);
    Holder<IoEvent> foreign = attach(other, link.socket.handle(), SLOT);
    EXPECT_EQ(listener.submit(foreign, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::INVALID_ARGUMENT);
    EXPECT_TRUE(listener.wait(0).empty());
}

TEST_F(EventListener, AttachOnInvalidSocketIsRejected)
{
    AttachResult invalid = listener.attach(kiotty::net::INVALID_SOCKET_HANDLE, SLOT);
    EXPECT_EQ(invalid.code(), SocketCode::INVALID_ARGUMENT);
    net::EventListener closed;
    AttachResult unopened = closed.attach(link.socket.handle(), SLOT);
    EXPECT_EQ(unopened.code(), SocketCode::CLOSED);
}

TEST_F(EventListener, SubmitWithNoneOrHoldKindIsRejectedWithInvalidArgument)
{
    char buffer[16];
    IoPayload payload = payloadOf(buffer, sizeof(buffer));
    IoAction rejected[] =
    {
        IoAction::none(),
        IoAction::hold(100),
        IoAction::startWork(),
        IoAction::close(KIOTTY_CLOSED)
    };
    for (const IoAction& action : rejected)
    {
        EXPECT_EQ(listener.submit(stream, action, payload), SocketCode::INVALID_ARGUMENT) << static_cast<int>(action.kind);
        EXPECT_FALSE(stream->pending());
    }
    EXPECT_EQ(listener.submit(stream, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, 0)), SocketCode::INVALID_ARGUMENT);
    EXPECT_EQ(listener.submit(stream, IoAction::write(IoPurpose::USER, 0), payloadOf(nullptr, 4)), SocketCode::INVALID_ARGUMENT);
    EXPECT_FALSE(stream->pending());
    EXPECT_TRUE(listener.wait(0).empty());
}

TEST_F(EventListener, WaitReturnsAtMostCompletionCapacityAndKeepsTheRestForNextWait)
{
    const size_t capacity = 4;
    net::EventListener narrow;
    ASSERT_EQ(narrow.open(capacity), SocketCode::SUCCESS);
    Link links[3];
    Holder<IoEvent> events[6];
    char bytes[] = "z";
    for (size_t i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(connect(links[i]));
        int32_t slot = static_cast<int32_t>(i);
        events[2 * i] = attach(narrow, links[i].socket.handle(), slot);
        events[2 * i + 1] = attach(narrow, links[i].socket.handle(), slot);
    }
    for (Holder<IoEvent>& event : events)
    {
        ASSERT_EQ(narrow.submit(event, IoAction::write(IoPurpose::USER, 0), payloadOf(bytes, 1)), SocketCode::SUCCESS);
    }
    for (Link& each : links)
    {
        char received[2];
        ASSERT_TRUE(each.peer.recvExactly(received, sizeof(received), WAIT_MS));
    }
    size_t total = 0;
    size_t largest = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    while (total < 6 && kiotty_test::elapsedMs(started) < WAIT_MS)
    {
        Span<const IoCompletion> waited = narrow.wait(10);
        EXPECT_LE(waited.length, capacity);
        total += waited.length;
        largest = std::max(largest, waited.length);
    }
    EXPECT_EQ(total, 6u);
    EXPECT_EQ(largest, capacity);
    EXPECT_TRUE(narrow.wait(50).empty());
}

TEST_F(EventListener, CloseWithPendingRequestsDoesNotCrashAndLaterWaitIsRefused)
{
    Holder<IoEvent> event = attach(listener, link.socket.handle(), SLOT);
    char buffer[16];
    ASSERT_EQ(listener.submit(event, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::SUCCESS);
    listener.close();
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    EXPECT_TRUE(listener.wait(1000).empty());
    EXPECT_LT(kiotty_test::elapsedMs(started), 500u);
    EXPECT_EQ(listener.submit(event, IoAction::read(IoPurpose::USER, 0, 1), payloadOf(buffer, sizeof(buffer))), SocketCode::CLOSED);
    EXPECT_EQ(listener.wake(), SocketCode::CLOSED);
    link.socket.close();
    listener.close();
}

#if defined(_WIN32)
class EventListener_Iocp : public EventListener
{
};

TEST_F(EventListener_Iocp, AcceptExBufferReceivesBothAddressesAndUpdateContextSucceeds)
{
    PassiveSocket passive;
    ASSERT_EQ(passive.open(loopback(0), 4), SocketCode::SUCCESS);
    Holder<IoEvent> event = attach(listener, passive.handle(), -1);
    AcceptTarget target;
    ASSERT_EQ(listener.submit(event, IoAction::accept(), target.payload()), SocketCode::SUCCESS);
    LoopbackPeer client;
    ASSERT_TRUE(client.connectTcp(passive.port()));
    std::vector<IoCompletion> completions = collect(listener, 1, WAIT_MS);
    ASSERT_EQ(completions.size(), 1u);
    ASSERT_EQ(completions[0].code, SocketCode::SUCCESS);

    SOCKET accepted = static_cast<SOCKET>(target.buffer().socket);
    SOCKET listening = static_cast<SOCKET>(passive.handle());
    GUID guid = WSAID_GETACCEPTEXSOCKADDRS;
    LPFN_GETACCEPTEXSOCKADDRS addresses_of = nullptr;
    DWORD returned = 0;
    ASSERT_EQ(::WSAIoctl(listening, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
        &addresses_of, sizeof(addresses_of), &returned, nullptr, nullptr), 0);
    sockaddr* local = nullptr;
    sockaddr* remote = nullptr;
    int local_length = 0;
    int remote_length = 0;
    DWORD address_length = static_cast<DWORD>(AcceptBuffer::ADDRESS_LENGTH);
    addresses_of(target.buffer().addresses, 0, address_length, address_length, &local, &local_length, &remote, &remote_length);
    ASSERT_NE(local, nullptr);
    ASSERT_NE(remote, nullptr);
    EXPECT_EQ(local->sa_family, AF_INET);
    EXPECT_EQ(remote->sa_family, AF_INET);
    EXPECT_EQ(::ntohs(reinterpret_cast<sockaddr_in*>(local)->sin_port), passive.port());

    EXPECT_EQ(::setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
        reinterpret_cast<const char*>(&listening), sizeof(listening)), 0);
    sockaddr_in peer;
    int peer_length = sizeof(peer);
    EXPECT_EQ(::getpeername(accepted, reinterpret_cast<sockaddr*>(&peer), &peer_length), 0);
}
#endif

#if defined(__linux__)
class EventListener_Epoll : public EventListener
{
};

TEST_F(EventListener_Epoll, NoCompletionArrivesWithoutASubmitEvenIfPeerSends)
{
    ASSERT_TRUE(link.peer.sendAll("unasked", 7));
    EXPECT_TRUE(listener.wait(50).empty());
}
#endif
