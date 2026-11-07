#include "base/connection/kiotty_connection.h"

#include "base/connection/kiotty_sender.h"
#include "data/datasource/kiotty_memory_layout.h"
#include "core/kiotty_clock.h"
#include "core/kiotty_deadline.h"

#include <cstring>
#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const INVALID_ACTION_MESSAGE = "action rejected: invalid argument";
        const char* const BUSY_ACTION_MESSAGE = "action rejected: io outstanding. use notify";
        const char* const HANDSHAKE_TIMEOUT_MESSAGE = "tls: handshake timeout";
        const char* const STAGE_FULL_MESSAGE = "receive: stage is full before the read condition";

        typedef Result<KiottyNetSpan, KiottyNetCode> ReadyResult;

        KiottyNetSpan span(const char* data, size_t length)
        {
            KiottyNetSpan result = { data, length };
            return result;
        }

        bool fits(size_t requested, size_t maximum)
        {
            return requested > 0 && requested <= maximum;
        }

        ReadyResult exactly(const IoAction& action, StageBase& stage)
        {
            size_t exact = action.as.read.exact;
            if (exact > stage.capacity())
            {
                return ReadyResult::failure(KIOTTY_NET_CODE_REQUEST_TOO_LARGE);
            }
            StageBase::Consumer in = stage.consumer();
            if (in.readable() < exact)
            {
                return ReadyResult::success(span(nullptr, 0));
            }
            return ReadyResult::success(span(in.peek(), exact));
        }

        ReadyResult until(IoAction& action, StageBase& stage)
        {
            size_t length = action.as.read_until.length;
            size_t max = action.as.read_until.max;
            if (length == 0)
            {
                return ReadyResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            }
            StageBase::Consumer in = stage.consumer();
            size_t readable = in.readable();
            const char* data = in.peek();
            size_t limit = readable < max ? readable : max;
            size_t position = action.as.read_until.scanned;
            while (position + length <= limit)
            {
                if (std::memcmp(data + position, action.as.read_until.delimiter, length) == 0)
                {
                    return ReadyResult::success(span(data, position + length));
                }
                position += 1;
            }
            action.as.read_until.scanned = position;
            if (readable >= max)
            {
                return ReadyResult::failure(KIOTTY_NET_CODE_PROTOCOL_ERROR);
            }
            return ReadyResult::success(span(nullptr, 0));
        }

        ReadyResult satisfied(IoAction& action, StageBase& stage)
        {
            if (action.kind == IoActionKind::READ)
            {
                return exactly(action, stage);
            }
            return until(action, stage);
        }

        bool validDelimiter(KiottyNetSpan delimiter, size_t max)
        {
            if (delimiter.data == nullptr)
            {
                return false;
            }
            if (delimiter.length == 0 || delimiter.length > IoAction::MAX_DELIMITER_LENGTH)
            {
                return false;
            }
            return max >= delimiter.length;
        }

        bool validWrite(KiottyNetSpan data)
        {
            return data.data != nullptr && data.length > 0;
        }
    }

    Connection::Connection()
        : _phase(Phase::IDLE)
        , _deadline(0)
        , _work()
        , _read_event()
        , _write_event()
        , _plain()
        , _tls()
        , _socket()
        , _id()
        , _context(nullptr)
        , _info()
        , _state(0)
    {
        _id.generation = 0;
        _id.index = -1;
    }

    Connection::~Connection() = default;

    KiottyNetCode Connection::initialize(PoolHandle id, ConnectionConfiguration&& configuration)
    {
        ConnectionLimits limits = configuration.limits;
        bool receive = fits(limits.receive, _plain.receive.maximum());
        bool send = fits(limits.send, _plain.send.maximum());
        bool notify = fits(limits.notify, _plain.notify.maximum());
        if (!receive || !send || !notify)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        _plain.limit(limits.receive, limits.send, limits.notify);
        _tls.limit(limits.receive, largerOf(limits.send, limits.notify));
        _id = id;
        _socket = std::move(configuration.socket);
        _info = _socket.peer();
        _context = configuration.context;
        _deadline = 0;
        _state.store(0);
        if (!configuration.profile.valid())
        {
            _phase.store(Phase::READY);
            return KIOTTY_NET_CODE_SUCCESS;
        }
        const char* hostname = configuration.hostname == nullptr ? "" : configuration.hostname;
        KiottyNetCode opened = _tls.open(configuration.profile, hostname);
        if (opened != KIOTTY_NET_CODE_SUCCESS)
        {
            return opened;
        }
        _phase.store(Phase::HANDSHAKING);
        return KIOTTY_NET_CODE_SUCCESS;
    }

    void Connection::reset() noexcept
    {
        _socket.close();
        _tls.close();
        _work.reset();
        _plain.clear();
        _deadline = 0;
        _context = nullptr;
        _info = KiottyNetConnectionInfo();
        _state.store(0);
        _phase.store(Phase::IDLE);
    }

    PoolHandle Connection::id() const
    {
        return _id;
    }

    int32_t Connection::generation() const
    {
        return _id.generation;
    }

    bool Connection::alive() const
    {
        Phase phase = _phase.load();
        return phase == Phase::HANDSHAKING || phase == Phase::READY;
    }

    Phase Connection::phase() const
    {
        return _phase.load();
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
        IoPayload empty = { nullptr, 0 };
        switch (action.kind)
        {
        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
        {
            StageBase& in = receiveStage(action.purpose);
            size_t room = in.provider().writable();
            IoPayload payload = { in.provider().prepare(room), room };
            return payload;
        }
        case IoActionKind::WRITE:
        {
            if (_tls.active())
            {
                StageBase::Consumer out = _tls.cipherSend().consumer();
                IoPayload payload = { out.peek(), out.readable() };
                return payload;
            }
            StageBase& out = action.purpose == IoPurpose::NOTIFY ? static_cast<StageBase&>(_plain.notify) : static_cast<StageBase&>(_plain.send);
            IoPayload payload = { out.consumer().peek(), action.as.write.length };
            return payload;
        }
        case IoActionKind::NONE:
        case IoActionKind::HOLD:
        case IoActionKind::START_WORK:
        case IoActionKind::CLOSE:
        case IoActionKind::ACCEPT:
            return empty;
        }
        return empty;
    }

    SocketCode Connection::attach(EventListener& listener)
    {
        SocketCode read = listener.attach(_read_event, socket(), slot());
        if (read != SocketCode::SUCCESS)
        {
            return read;
        }
        SocketCode write = listener.attach(_write_event, socket(), slot());
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

    IoAction Connection::onOpened(const Handler& handler)
    {
        if (_tls.active())
        {
            setDeadline(TLS_HANDSHAKE_TIMEOUT_MS);
            return apply(onHandshake(handler), handler);
        }
        KiottyNetConnectionAction returned = handler.callbacks.onReady(_context, handle(handler));
        return apply(drive(admit(returned, handler), handler), handler);
    }

    IoAction Connection::close(KiottyNetCode reason, const Handler& handler)
    {
        if (!alive())
        {
            return IoAction::none();
        }
        return apply(IoAction::close(reason), handler);
    }

    IoAction Connection::dispatch(const IoCompletion& completion, const Handler& handler, std::mutex& notification)
    {
        if (!alive())
        {
            return IoAction::none();
        }
        IoAction action = IoAction::none();
        switch (completion.kind)
        {
        case IoKind::RECEIVE:
            action = onReceived(completion, handler);
            break;
        case IoKind::SEND:
            action = onSent(completion, handler, notification);
            break;
        case IoKind::TIMEOUT:
            action = onTimeout(handler);
            break;
        case IoKind::WORK_READY:
            action = onWorkReady(handler);
            break;
        case IoKind::WORK_DONE:
            action = onWorkFinished(completion, handler);
            break;
        case IoKind::ACCEPT:
        case IoKind::WAKE:
            break;
        }
        return apply(action, handler);
    }

    IoAction Connection::pickWrite(std::mutex& notification)
    {
        if (!alive())
        {
            return IoAction::none();
        }
        if (_work.working())
        {
            return IoAction::none();
        }
        if (_write_event.pending)
        {
            return IoAction::none();
        }
        return select(notification);
    }

    uint64_t Connection::deadline() const
    {
        return _deadline;
    }

    bool Connection::expireDeadline(uint64_t now)
    {
        if (_deadline == 0 || now < _deadline)
        {
            return false;
        }
        _deadline = 0;
        return true;
    }

    bool Connection::workReady() const
    {
        return _work.requested() && !_read_event.pending && !_write_event.pending;
    }

    bool Connection::workFinished() const
    {
        return _work.finished();
    }

    IoAction Connection::takeWorkOutcome()
    {
        return _work.onFinished();
    }

    bool Connection::working() const
    {
        return _work.working();
    }

    const IoEvent& Connection::readEvent() const
    {
        return _read_event;
    }

    const IoEvent& Connection::writeEvent() const
    {
        return _write_event;
    }

    KiottyNetConnectionInfo Connection::info() const
    {
        return _info;
    }

    KiottyNetConnectionContext Connection::context() const
    {
        return _context;
    }

    int32_t Connection::state() const
    {
        return _state.load();
    }

    void Connection::setState(int32_t state)
    {
        _state.store(state);
    }

    size_t Connection::notificationWritable()
    {
        return _plain.notify.provider().writable();
    }

    KiottyNetCode Connection::stageNotification(KiottyNetSpan data)
    {
        return Sender::stage(_plain.notify, data);
    }

    KiottyNetCode Connection::verifyWork(int32_t generation) const
    {
        return _work.verify(generation);
    }

    ConnectionStream Connection::stream()
    {
        return ConnectionStream(_socket, _tls, _plain.receive, _plain.send);
    }

    KiottyNetCode Connection::closeStream(int32_t generation, const KiottyNetConnectionAction& then)
    {
        KiottyNetCode verified = _work.verify(generation);
        if (verified != KIOTTY_NET_CODE_SUCCESS)
        {
            return verified;
        }
        if (then.type == KIOTTY_NET_ACTION_TYPE_START_WORK)
        {
            return KIOTTY_NET_CODE_INVALID_ARGUMENT;
        }
        AdoptResult outcome = adopt(then);
        if (!outcome)
        {
            return outcome.code;
        }
        _work.finish(outcome.value);
        if (_phase.load() == Phase::CLOSING)
        {
            return KIOTTY_NET_CODE_CLOSED;
        }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    IoAction Connection::onReceived(const IoCompletion& completion, const Handler& handler)
    {
        if (completion.code != SocketCode::SUCCESS)
        {
            return onPeerGone(completion.code);
        }
        receiveStage(completion.action.purpose).provider().commit(completion.length);
        if (completion.action.purpose == IoPurpose::HANDSHAKE)
        {
            return onHandshake(handler);
        }
        return drive(completion.action, handler);
    }

    IoAction Connection::onSent(const IoCompletion& completion, const Handler& handler, std::mutex& notification)
    {
        if (completion.code != SocketCode::SUCCESS)
        {
            return onPeerGone(completion.code);
        }
        if (_tls.active())
        {
            _tls.cipherSend().consumer().commit(completion.length);
        }
        size_t plain = completion.action.as.write.length;
        switch (completion.action.purpose)
        {
        case IoPurpose::HANDSHAKE:
            return onHandshake(handler);
        case IoPurpose::CONTROL:
            return IoAction::none();
        case IoPurpose::NOTIFY:
        {
            std::lock_guard<std::mutex> lock(notification);
            Sender::release(_plain.notify, plain);
            return IoAction::none();
        }
        case IoPurpose::USER:
        {
            Sender::release(_plain.send, plain);
            KiottyNetConnectionAction returned = handler.callbacks.onWritten(_context, handle(handler), plain);
            return drive(admit(returned, handler), handler);
        }
        }
        return IoAction::none();
    }

    IoAction Connection::onTimeout(const Handler& handler)
    {
        if (_phase.load() == Phase::HANDSHAKING)
        {
            reject(handler, KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT, HANDSHAKE_TIMEOUT_MESSAGE);
            return IoAction::close(KIOTTY_NET_CODE_TLS_HANDSHAKE_TIMEOUT);
        }
        KiottyNetConnectionAction returned = handler.callbacks.onTimeout(_context, handle(handler));
        if (userActionPending())
        {
            return extend(returned, handler);
        }
        return drive(admit(returned, handler), handler);
    }

    IoAction Connection::onWorkReady(const Handler& handler)
    {
        _work.start(generation());
        handler.callbacks.onWorkStarted(_context, streamHandle(handler));
        return IoAction::none();
    }

    IoAction Connection::onWorkFinished(const IoCompletion& completion, const Handler& handler)
    {
        return drive(completion.action, handler);
    }

    IoAction Connection::onHandshake(const Handler& handler)
    {
        switch (_tls.handshake())
        {
        case TlsStep::SEND:
            return IoAction::none();
        case TlsStep::RECEIVE:
            return IoAction::read(IoPurpose::HANDSHAKE, TLS_HANDSHAKE_TIMEOUT_MS, 0);
        case TlsStep::DONE:
        {
            _phase.store(Phase::READY);
            KiottyNetConnectionAction returned = handler.callbacks.onReady(_context, handle(handler));
            return drive(admit(returned, handler), handler);
        }
        case TlsStep::FAILED:
            break;
        }
        reject(handler, _tls.lastCode(), _tls.lastMessage());
        return IoAction::close(_tls.lastCode());
    }

    IoAction Connection::onPeerGone(SocketCode code)
    {
        if (code == SocketCode::CLOSED)
        {
            return IoAction::close(KIOTTY_NET_CODE_PEER_CLOSED);
        }
        return IoAction::close(KIOTTY_NET_CODE_SOCKET_ERROR);
    }

    AdoptResult Connection::adopt(const KiottyNetConnectionAction& returned)
    {
        switch (returned.type)
        {
        case KIOTTY_NET_ACTION_TYPE_HOLD:
        case KIOTTY_NET_ACTION_TYPE_START_WORK:
        case KIOTTY_NET_ACTION_TYPE_CLOSE:
            return AdoptResult::success(IoAction::from(returned));
        case KIOTTY_NET_ACTION_TYPE_READ:
            if (returned.as.read.exact == 0)
            {
                return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            }
            return AdoptResult::success(IoAction::from(returned));
        case KIOTTY_NET_ACTION_TYPE_READ_UNTIL:
            if (!validDelimiter(returned.as.read_until.delimiter, returned.as.read_until.max))
            {
                return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            }
            return AdoptResult::success(IoAction::from(returned));
        case KIOTTY_NET_ACTION_TYPE_WRITE:
        {
            if (!validWrite(returned.as.write.data))
            {
                return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
            }
            KiottyNetCode staged = Sender::stage(_plain.send, returned.as.write.data);
            if (staged != KIOTTY_NET_CODE_SUCCESS)
            {
                return AdoptResult::failure(staged);
            }
            return AdoptResult::success(IoAction::hold(returned.timeout_milliseconds));
        }
        }
        return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
    }

    IoAction Connection::admit(const KiottyNetConnectionAction& returned, const Handler& handler)
    {
        AdoptResult adopted = adopt(returned);
        if (adopted)
        {
            return adopted.value;
        }
        if (adopted.code == KIOTTY_NET_CODE_INVALID_ARGUMENT)
        {
            reject(handler, KIOTTY_NET_CODE_INVALID_ARGUMENT, INVALID_ACTION_MESSAGE);
        }
        return IoAction::close(adopted.code);
    }

    IoAction Connection::drive(IoAction action, const Handler& handler)
    {
        for (;;)
        {
            switch (action.kind)
            {
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
                return action;
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
            {
                ReadyResult ready = satisfied(action, _plain.receive);
                if (!ready)
                {
                    return IoAction::close(ready.code);
                }
                KiottyNetSpan data = ready.value;
                if (data.length == 0)
                {
                    Result<size_t, KiottyNetCode> more = refill();
                    if (!more)
                    {
                        const char* message = more.code == KIOTTY_NET_CODE_REQUEST_TOO_LARGE ? STAGE_FULL_MESSAGE : _tls.lastMessage();
                        reject(handler, more.code, message);
                        return IoAction::close(more.code);
                    }
                    if (more.value == 0)
                    {
                        return action;
                    }
                    break;
                }
                KiottyNetConnectionAction next = handler.callbacks.onRead(_context, handle(handler), data);
                _plain.receive.consumer().commit(data.length);
                action = admit(next, handler);
                break;
            }
            case IoActionKind::NONE:
            case IoActionKind::WRITE:
            case IoActionKind::ACCEPT:
                return IoAction::none();
            }
        }
    }

    IoAction Connection::extend(const KiottyNetConnectionAction& returned, const Handler& handler)
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
            reject(handler, KIOTTY_NET_CODE_BUSY, BUSY_ACTION_MESSAGE);
            return IoAction::hold(returned.timeout_milliseconds);
        case KIOTTY_NET_ACTION_TYPE_START_WORK:
            reject(handler, KIOTTY_NET_CODE_BUSY, BUSY_ACTION_MESSAGE);
            return IoAction::hold(0);
        }
        reject(handler, KIOTTY_NET_CODE_INVALID_ARGUMENT, INVALID_ACTION_MESSAGE);
        return IoAction::close(KIOTTY_NET_CODE_INVALID_ARGUMENT);
    }

    Result<size_t, KiottyNetCode> Connection::refill()
    {
        _plain.receive.compact();
        size_t produced = 0;
        if (_tls.active())
        {
            DecodeResult decoded = _tls.decode(_plain.receive);
            if (!decoded)
            {
                return Result<size_t, KiottyNetCode>::failure(decoded.code);
            }
            produced = decoded.value;
        }
        if (produced == 0 && _plain.receive.provider().writable() == 0)
        {
            return Result<size_t, KiottyNetCode>::failure(KIOTTY_NET_CODE_REQUEST_TOO_LARGE);
        }
        return Result<size_t, KiottyNetCode>::success(produced);
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
            _deadline = 0;
            _work.request();
            return action;
        case IoActionKind::CLOSE:
            _deadline = 0;
            _phase.store(Phase::CLOSING);
            _work.abandon();
            handler.callbacks.onDisconnected(handler.app_context, _context, handle(handler), action.as.close.reason);
            return action;
        case IoActionKind::NONE:
        case IoActionKind::WRITE:
        case IoActionKind::ACCEPT:
            return action;
        }
        return action;
    }

    IoAction Connection::select(std::mutex& notification)
    {
        if (_tls.active() && _tls.cipherSend().consumer().readable() > 0)
        {
            IoPurpose purpose = _phase.load() == Phase::HANDSHAKING ? IoPurpose::HANDSHAKE : IoPurpose::CONTROL;
            return IoAction::write(purpose, 0, 0);
        }
        if (_phase.load() == Phase::HANDSHAKING)
        {
            return IoAction::none();
        }
        {
            std::lock_guard<std::mutex> lock(notification);
            StageBase::Consumer notify = _plain.notify.consumer();
            size_t plain = notify.readable();
            if (plain > 0)
            {
                Span<char> picked = { notify.peek(), plain };
                return encoded(IoAction::write(IoPurpose::NOTIFY, 0, plain), picked);
            }
        }
        StageBase::Consumer send = _plain.send.consumer();
        size_t plain = send.readable();
        if (plain == 0)
        {
            return IoAction::none();
        }
        Span<char> picked = { send.peek(), plain };
        return encoded(IoAction::write(IoPurpose::USER, 0, plain), picked);
    }

    IoAction Connection::encoded(IoAction write, Span<char> plain)
    {
        if (_tls.active())
        {
            _tls.encode(span(plain.data, plain.length));
        }
        return write;
    }

    bool Connection::userActionPending()
    {
        return _read_event.pending || _plain.send.consumer().readable() > 0;
    }

    void Connection::setDeadline(uint32_t timeout_milliseconds)
    {
        if (timeout_milliseconds == 0)
        {
            _deadline = 0;
            return;
        }
        _deadline = monotonicMilliseconds() + timeout_milliseconds;
    }

    void Connection::reject(const Handler& handler, KiottyNetCode code, const char* message) const
    {
        handler.callbacks.onError(handler.app_context, _info, code, message);
    }

    StageBase& Connection::receiveStage(IoPurpose purpose)
    {
        if (purpose == IoPurpose::HANDSHAKE)
        {
            return _tls.cipherReceive();
        }
        if (_tls.active())
        {
            return _tls.cipherReceive();
        }
        return _plain.receive;
    }

    KiottyNetConnectionHandle Connection::handle(const Handler& handler) const
    {
        KiottyNetConnectionHandle connection = { &handler.owner, _id.value() };
        return connection;
    }

    KiottyNetConnectionStream Connection::streamHandle(const Handler& handler) const
    {
        KiottyNetConnectionStream stream = { &handler.owner, _id.value() };
        return stream;
    }
}
