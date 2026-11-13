#include "data/repository/connection/kiotty_send_flow.h"

#include "core/kiotty_clock.h"
#include "data/repository/connection/kiotty_sender.h"

namespace kiotty::net::implementation
{
    namespace
    {
        bool    hasCipherWaiting(TlsEngine& tls);
        IoPiece pieceOf(StageBase& stage, size_t length);
    }

    SendFlow::SendFlow(const ConnectionParts& parts)
        : _send(parts.send)
        , _notify(parts.notify)
        , _tls(parts.tls)
        , _write_event(parts.write_event)
        , _work(parts.work)
        , _phase(parts.phase)
        , _deadline(0)
        , _timeout(0)
    {
    }

    IoAction SendFlow::pick(const std::lock_guard<std::mutex>& notification)
    {
        (void)notification;
        const bool blocked = isBlocked();
        if (blocked)
        {
            return IoAction::none();
        }
        const IoAction write = select();
        if (write.kind == IoActionKind::WRITE)
        {
            startDeadline();
        }
        return write;
    }

    IoPayload SendFlow::payload(const IoAction& write)
    {
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            StageBase::Consumer cipher = _tls.stages().send.consumer();
            return IoPayload::single(cipher.peek(), cipher.readable());
        }
        const IoPiece sent = pieceOf(_send, write.as.write.length);
        const IoPiece notified = pieceOf(_notify, write.as.write.notify_length);
        return IoPayload::pair(sent, notified);
    }

    void SendFlow::release(const IoCompletion& sent, std::mutex& notification)
    {
        clearDeadline();
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            _tls.stages().send.consumer().commit(sent.length);
        }
        const bool is_plain = sent.action.purpose == IoPurpose::USER
                              || sent.action.purpose == IoPurpose::NOTIFY;
        if (!is_plain)
        {
            return;
        }
        const size_t notified = sent.action.as.write.notify_length;
        if (notified > 0)
        {
            const std::lock_guard<std::mutex> lock(notification);
            Sender::release(_notify, notified);
        }
        const size_t length = sent.action.as.write.length;
        if (length > 0)
        {
            Sender::release(_send, length);
        }
    }

    size_t SendFlow::notificationWritable()
    {
        return _notify.provider().writable();
    }

    KiottyNetCode SendFlow::stageNotification(Span<const KiottyNetSpan> pieces)
    {
        return Sender::stage(_notify, pieces);
    }

    void SendFlow::limit(uint32_t timeout_milliseconds)
    {
        _timeout = timeout_milliseconds;
        _deadline.store(0);
    }

    void SendFlow::clearDeadline()
    {
        _deadline.store(0);
    }

    uint64_t SendFlow::deadline() const
    {
        return _deadline.load();
    }

    bool SendFlow::isDrained()
    {
        const size_t staged = _send.consumer().readable();
        const bool cipher_waiting = hasCipherWaiting(_tls);
        return staged == 0 && !cipher_waiting;
    }

    IoAction SendFlow::select()
    {
        const Phase phase = _phase.load();
        const bool cipher_waiting = hasCipherWaiting(_tls);
        if (cipher_waiting)
        {
            const bool is_handshaking = phase == Phase::HANDSHAKING;
            const IoPurpose purpose = is_handshaking ? IoPurpose::HANDSHAKE : IoPurpose::CONTROL;
            return IoAction::write(purpose, 0, 0);
        }
        if (phase == Phase::HANDSHAKING)
        {
            return IoAction::none();
        }
        const size_t length = _send.consumer().readable();
        const bool is_flushing = phase == Phase::FLUSHING;
        const size_t notify_length = is_flushing ? 0 : _notify.consumer().readable();
        if (length == 0 && notify_length == 0)
        {
            return IoAction::none();
        }
        return encoded(IoAction::gather(length, notify_length));
    }

    IoAction SendFlow::encoded(IoAction write)
    {
        const bool secure = _tls.status().is_active;
        if (!secure)
        {
            return write;
        }
        const IoPiece sent = pieceOf(_send, write.as.write.length);
        const IoPiece notified = pieceOf(_notify, write.as.write.notify_length);
        _tls.encode(KiottyNetSpan{ static_cast<const char*>(sent.buffer), sent.length });
        _tls.encode(KiottyNetSpan{ static_cast<const char*>(notified.buffer), notified.length });
        return write;
    }

    bool SendFlow::isBlocked() const
    {
        const bool open = isOpen(_phase.load());
        const bool working = _work.state() != WorkContext::State::IDLE;
        return !open || working || _write_event.pending;
    }

    void SendFlow::startDeadline()
    {
        if (_timeout == 0)
        {
            _deadline.store(0);
            return;
        }
        _deadline.store(monotonicMilliseconds() + _timeout);
    }

    namespace
    {
        bool hasCipherWaiting(TlsEngine& tls)
        {
            const bool secure = tls.status().is_active;
            if (!secure)
            {
                return false;
            }
            return tls.stages().send.consumer().readable() > 0;
        }

        IoPiece pieceOf(StageBase& stage, size_t length)
        {
            if (length == 0)
            {
                return IoPiece{ nullptr, 0 };
            }
            return IoPiece{ stage.consumer().peek(), length };
        }
    }
}
