#include "data/repository/connection/kiotty_send_flow.h"

#include "data/repository/connection/kiotty_sender.h"

namespace kiotty::net::implementation
{
    namespace
    {
        bool hasCipherWaiting(TlsEngine& tls);
    }

    SendFlow::SendFlow(const ConnectionParts& parts)
        : _send(parts.send)
        , _notify(parts.notify)
        , _tls(parts.tls)
        , _write_event(parts.write_event)
        , _work(parts.work)
        , _phase(parts.phase)
    {
    }

    IoAction SendFlow::pick(std::mutex& notification)
    {
        const bool blocked = isBlocked();
        if (blocked)
        {
            return IoAction::none();
        }
        return select(notification);
    }

    IoPayload SendFlow::payload(const IoAction& write)
    {
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            StageBase::Consumer cipher = _tls.stages().send.consumer();
            return IoPayload{ cipher.peek(), cipher.readable() };
        }
        StageBase& plain = write.purpose == IoPurpose::NOTIFY ? _notify : _send;
        return IoPayload{ plain.consumer().peek(), write.as.write.length };
    }

    void SendFlow::release(const IoCompletion& sent, std::mutex& notification)
    {
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            _tls.stages().send.consumer().commit(sent.length);
        }
        const size_t plain = sent.action.as.write.length;
        switch (sent.action.purpose)
        {
        case IoPurpose::HANDSHAKE:
        case IoPurpose::CONTROL:
            return;
        case IoPurpose::NOTIFY:
        {
            const std::lock_guard<std::mutex> lock(notification);
            Sender::release(_notify, plain);
            return;
        }
        case IoPurpose::USER:
            Sender::release(_send, plain);
            return;
        }
    }

    size_t SendFlow::notificationWritable()
    {
        return _notify.provider().writable();
    }

    KiottyNetCode SendFlow::stageNotification(KiottyNetSpan data)
    {
        return Sender::stage(_notify, data);
    }

    IoAction SendFlow::select(std::mutex& notification)
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
        const IoAction pushed = selectNotification(notification);
        if (pushed.kind == IoActionKind::WRITE)
        {
            return pushed;
        }
        return selectStage(_send, IoPurpose::USER);
    }

    IoAction SendFlow::selectNotification(std::mutex& notification)
    {
        const std::lock_guard<std::mutex> lock(notification);
        return selectStage(_notify, IoPurpose::NOTIFY);
    }

    IoAction SendFlow::selectStage(StageBase& stage, IoPurpose purpose)
    {
        StageBase::Consumer waiting = stage.consumer();
        const size_t plain = waiting.readable();
        if (plain == 0)
        {
            return IoAction::none();
        }
        return encoded(IoAction::write(purpose, 0, plain), Span<char>{ waiting.peek(), plain });
    }

    IoAction SendFlow::encoded(IoAction write, Span<char> plain)
    {
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            _tls.encode(KiottyNetSpan{ plain.data, plain.length });
        }
        return write;
    }

    bool SendFlow::isBlocked() const
    {
        const bool alive = isAlive(_phase.load());
        const bool working = _work.state() != WorkContext::State::IDLE;
        return !alive || working || _write_event.pending;
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
    }
}
