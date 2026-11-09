#include "data/repository/connection/kiotty_connection.h"

#include "core/kiotty_clock.h"
#include "data/datasource/kiotty_memory_layout.h"
#include "domain/entity/kiotty_connection_event.h"

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const BUSY_ACTION_MESSAGE = "action rejected: io outstanding. use notify";
        const char* const HANDSHAKE_TIMEOUT_MESSAGE = "tls: handshake timeout";
        const char* const WORK_CLOSE_MESSAGE = "work rejected: the work started action is invalid";

        bool     fits(size_t requested, size_t maximum);
        IoAction closedBy(SocketCode code);
    }

    Connection::Connection()
        : _plain()
        , _tls()
        , _socket()
        , _info()
        , _read_event()
        , _write_event()
        , _work()
        , _work_flow(parts())
        , _send_flow(parts())
        , _receive_flow(parts())
        , _deadline(0)
        , _context(nullptr)
        , _id{ 0, -1 }
        , _machine_state(0)
        , _phase(Phase::IDLE)
    {
    }

    Connection::~Connection() = default;

    KiottyNetCode Connection::initialize(PoolHandle id, ConnectionConfiguration&& configuration)
    {
        const ConnectionLimits limits = configuration.limits;
        const bool receive = fits(limits.receive, MemoryLimits::RECEIVE_CAPACITY)
                             && fits(limits.receive, _plain.receive.maximum());
        const bool send = fits(limits.send, MemoryLimits::SEND_CAPACITY)
                          && fits(limits.send, _plain.send.maximum());
        const bool notify = fits(limits.notify, MemoryLimits::PUSH_CAPACITY)
                            && fits(limits.notify, _plain.notify.maximum());
        if (!receive || !send || !notify) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _plain.limit(limits.receive, limits.send, limits.notify);
        _id = id;
        _socket = std::move(configuration.socket);
        _info = _socket.peer();
        _context = configuration.context;
        _deadline.store(0);
        _machine_state = 0;
        const TlsCapacity capacity{ limits.receive, largerOf(limits.send, limits.notify) };
        return open(configuration.profile, configuration.hostname, capacity);
    }

    void Connection::reset() noexcept
    {
        _socket.close();
        _tls.close();
        _work.reset();
        _plain.clear();
        _deadline.store(0);
        _context = nullptr;
        _info = KiottyNetConnectionInfo{};
        _machine_state = 0;
        _phase.store(Phase::IDLE);
    }

    IoAction Connection::onOpened(const Handler& handler)
    {
        _machine_state = handler.initialState();
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            setDeadline(TLS_HANDSHAKE_TIMEOUT_MILLISECONDS);
            return apply(onHandshake(handler), handler);
        }
        return apply(onReady(handler), handler);
    }

    IoAction Connection::dispatch(const IoCompletion& completion, const Handler& handler,
                                  std::mutex& notification)
    {
        const bool alive = isAlive(_phase.load());
        if (!alive) { return IoAction::none(); }
        return apply(react(completion, handler, notification), handler);
    }

    IoAction Connection::close(KiottyNetCode reason, const Handler& handler)
    {
        const bool alive = isAlive(_phase.load());
        if (!alive) { return IoAction::none(); }
        return apply(IoAction::close(reason), handler);
    }

    ConnectionState Connection::state() const
    {
        ConnectionState snapshot{};
        snapshot.deadline = _deadline.load();
        snapshot.context = _context;
        snapshot.id = _id;
        snapshot.info = _info;
        snapshot.phase = _phase.load();
        snapshot.is_alive = isAlive(snapshot.phase);
        snapshot.pending = _work_flow.pending();
        return snapshot;
    }

    ConnectionFlows Connection::flows()
    {
        return ConnectionFlows{ _receive_flow, _send_flow, _work_flow };
    }

    SocketHandle Connection::socket() const
    {
        return _socket.handle();
    }

    int32_t Connection::slot() const
    {
        return _id.index;
    }

    IoEvent& Connection::eventFor(const IoAction& action)
    {
        if (action.kind == IoActionKind::WRITE)
        {
            return _write_event;
        }
        return _read_event;
    }

    IoPayload Connection::resolve(const IoAction& action)
    {
        switch (action.kind)
        {
        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
            return _receive_flow.room(action.purpose);
        case IoActionKind::WRITE:
            return _send_flow.payload(action);
        case IoActionKind::NONE:
        case IoActionKind::HOLD:
        case IoActionKind::START_WORK:
        case IoActionKind::CLOSE:
        case IoActionKind::ACCEPT:
            return IoPayload{ nullptr, 0 };
        }
        return IoPayload{ nullptr, 0 };
    }

    SocketCode Connection::attach(EventListener& listener)
    {
        const SocketCode read = listener.attach(_read_event, socket(), slot());
        if (read != SocketCode::SUCCESS) { return read; }
        const SocketCode write = listener.attach(_write_event, socket(), slot());
        if (write != SocketCode::SUCCESS)
        {
            listener.detach(_read_event);
        }
        return write;
    }

    void Connection::detach(EventListener& listener)
    {
        listener.detach(_read_event);
        listener.detach(_write_event);
    }

    void Connection::shutdown()
    {
        (void)implementation::shutdownWrite(_socket);
    }

    KiottyNetCode Connection::open(TlsProfile& profile, Span<const char> hostname,
                                   TlsCapacity capacity)
    {
        const bool secure = profile.valid();
        if (!secure)
        {
            _phase.store(Phase::READY);
            return KIOTTY_NET_CODE_SUCCESS;
        }
        const KiottyNetCode opened = _tls.open(profile, hostname, capacity);
        if (opened != KIOTTY_NET_CODE_SUCCESS) { return opened; }
        _phase.store(Phase::HANDSHAKING);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    IoAction Connection::react(const IoCompletion& completion, const Handler& handler,
                               std::mutex& notification)
    {
        switch (completion.kind)
        {
        case IoKind::RECEIVE:
            return onReceived(completion, handler);
        case IoKind::SEND:
            return onSent(completion, handler, notification);
        case IoKind::TIMEOUT:
            return onTimeout(handler);
        case IoKind::WORK_READY:
            return onWorkReady(handler);
        case IoKind::WORK_DONE:
            return _receive_flow.drive(completion.action, callerFor(handler));
        case IoKind::ACCEPT:
        case IoKind::WAKE:
            return IoAction::none();
        }
        return IoAction::none();
    }

    IoAction Connection::onReceived(const IoCompletion& completion, const Handler& handler)
    {
        if (completion.code != SocketCode::SUCCESS) { return closedBy(completion.code); }
        _receive_flow.commit(completion.action.purpose, completion.length);
        if (completion.action.purpose == IoPurpose::HANDSHAKE)
        {
            return onHandshake(handler);
        }
        return _receive_flow.drive(completion.action, callerFor(handler));
    }

    IoAction Connection::onSent(const IoCompletion& completion, const Handler& handler,
                                std::mutex& notification)
    {
        if (completion.code != SocketCode::SUCCESS) { return closedBy(completion.code); }
        _send_flow.release(completion, notification);
        switch (completion.action.purpose)
        {
        case IoPurpose::HANDSHAKE:
            return onHandshake(handler);
        case IoPurpose::CONTROL:
        case IoPurpose::NOTIFY:
            return IoAction::none();
        case IoPurpose::USER:
            return onWritten(completion.action.as.write.length, handler);
        }
        return IoAction::none();
    }

    IoAction Connection::onWritten(size_t length, const Handler& handler)
    {
        const Caller caller = callerFor(handler);
        const KiottyNetConnectionAction returned = handler.dispatch(caller, writtenEvent(length));
        return _receive_flow.drive(caller.admit(returned), caller);
    }

    IoAction Connection::onTimeout(const Handler& handler)
    {
        _deadline.store(0);
        const Caller caller = callerFor(handler);
        const Phase phase = _phase.load();
        if (phase == Phase::HANDSHAKING)
        {
            caller.reject(KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT, HANDSHAKE_TIMEOUT_MESSAGE);
            return IoAction::close(KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT);
        }
        const KiottyNetConnectionAction returned = handler.dispatch(caller, timeoutEvent());
        const bool pending = isUserActionPending();
        if (pending)
        {
            return extend(returned, caller);
        }
        return _receive_flow.drive(caller.admit(returned), caller);
    }

    IoAction Connection::onWorkReady(const Handler& handler)
    {
        _work.start(_id.generation);
        const Caller caller = callerFor(handler);
        const KiottyNetConnectionEvent started = workStartedEvent(caller.stream());
        const KiottyNetConnectionAction returned = handler.dispatch(caller, started);
        if (returned.type == KIOTTY_NET_ACTION_TYPE_START_WORK) { return IoAction::none(); }
        const KiottyNetCode closed = _work_flow.closeStream(_id.generation, returned);
        if (closed != KIOTTY_NET_CODE_SUCCESS) { caller.reject(closed, WORK_CLOSE_MESSAGE); }
        return IoAction::none();
    }

    IoAction Connection::onHandshake(const Handler& handler)
    {
        const TlsStep step = _tls.handshake();
        switch (step)
        {
        case TlsStep::SEND:
            return IoAction::none();
        case TlsStep::RECEIVE:
            return IoAction::read(IoPurpose::HANDSHAKE, TLS_HANDSHAKE_TIMEOUT_MILLISECONDS, 0);
        case TlsStep::DONE:
            _phase.store(Phase::READY);
            return onReady(handler);
        case TlsStep::FAILED:
            break;
        }
        const TlsFailure rejected = _tls.status().failure;
        callerFor(handler).reject(rejected.code, rejected.message);
        return IoAction::close(rejected.code);
    }

    IoAction Connection::onReady(const Handler& handler)
    {
        const Caller caller = callerFor(handler);
        const KiottyNetConnectionAction returned = handler.dispatch(caller, connectedEvent());
        return _receive_flow.drive(caller.admit(returned), caller);
    }

    IoAction Connection::extend(const KiottyNetConnectionAction& returned, const Caller& caller)
    {
        switch (returned.type)
        {
        case KIOTTY_NET_ACTION_TYPE_CLOSE:
            return IoAction::close(returned.as.close.reason);
        case KIOTTY_NET_ACTION_TYPE_HOLD:
        case KIOTTY_NET_ACTION_TYPE_READ:
        case KIOTTY_NET_ACTION_TYPE_READ_UNTIL:
            return IoAction::hold(returned.timeout_milliseconds);
        case KIOTTY_NET_ACTION_TYPE_WRITE:
            caller.reject(KIOTTY_NET_CODE_BUSY, BUSY_ACTION_MESSAGE);
            return IoAction::hold(returned.timeout_milliseconds);
        case KIOTTY_NET_ACTION_TYPE_START_WORK:
            caller.reject(KIOTTY_NET_CODE_BUSY, BUSY_ACTION_MESSAGE);
            return IoAction::hold(0);
        }
        return caller.admit(returned);
    }

    IoAction Connection::apply(IoAction action, const Handler& handler)
    {
        switch (action.kind)
        {
        case IoActionKind::HOLD:
        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
            setDeadline(action.timeout_milliseconds);
            return action;
        case IoActionKind::START_WORK:
            _deadline.store(0);
            _work.request();
            return action;
        case IoActionKind::CLOSE:
            enterClosing(action.as.close.reason, handler);
            return action;
        case IoActionKind::NONE:
        case IoActionKind::WRITE:
        case IoActionKind::ACCEPT:
            return action;
        }
        return action;
    }

    void Connection::enterClosing(KiottyNetCode reason, const Handler& handler)
    {
        _deadline.store(0);
        _phase.store(Phase::CLOSING);
        _work.abandon();
        const Caller caller = callerFor(handler);
        static_cast<void>(handler.dispatch(caller, closedEvent(reason)));
    }

    bool Connection::isUserActionPending()
    {
        const size_t staged = _plain.send.consumer().readable();
        return _read_event.pending || staged > 0;
    }

    void Connection::setDeadline(uint32_t timeout_milliseconds)
    {
        if (timeout_milliseconds == 0)
        {
            _deadline.store(0);
            return;
        }
        _deadline.store(monotonicMilliseconds() + timeout_milliseconds);
    }

    namespace
    {
        bool fits(size_t requested, size_t maximum)
        {
            return requested > 0 && requested <= maximum;
        }

        IoAction closedBy(SocketCode code)
        {
            const bool is_peer_closed = code == SocketCode::CLOSED;
            if (is_peer_closed) { return IoAction::close(KIOTTY_NET_CODE_PEER_CLOSED); }
            return IoAction::close(KIOTTY_NET_CODE_SOCKET_ERROR);
        }
    }
}
