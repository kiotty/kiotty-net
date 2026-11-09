#include "data/repository/connection/kiotty_receive_flow.h"

#include "domain/entity/kiotty_connection_event.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const STAGE_FULL_MESSAGE = "receive: stage is full before the read condition";

        using ReadyResult = Result<KiottyNetSpan, KiottyNetCode>;

        bool        isRead(const IoAction& action);
        IoAction    settled(const IoAction& action);
        ReadyResult satisfied(IoAction& action, StageBase& stage);
        ReadyResult exactly(const IoAction& action, StageBase& stage);
        ReadyResult until(IoAction& action, StageBase& stage);
        size_t      findDelimiter(const IoAction& action, Span<const char> scanned);
        ReadyResult nothingYet();
    }

    ReceiveFlow::ReceiveFlow(const ConnectionParts& parts)
        : _receive(parts.receive)
        , _tls(parts.tls)
    {
    }

    IoAction ReceiveFlow::drive(IoAction action, const Caller& caller)
    {
        while (isRead(action))
        {
            const ReadyResult ready = satisfied(action, _receive);
            if (!ready) { return IoAction::close(ready.code); }
            if (ready.value.length > 0)
            {
                action = deliver(ready.value, caller);
                continue;
            }
            const RefillResult more = refill();
            if (!more) { return refuse(more.code, caller); }
            if (more.value == 0)
            {
                return action;
            }
        }
        return settled(action);
    }

    IoPayload ReceiveFlow::room(IoPurpose purpose)
    {
        StageBase::Provider provider = stageFor(purpose).provider();
        const size_t writable = provider.writable();
        return IoPayload{ provider.prepare(writable), writable };
    }

    void ReceiveFlow::commit(IoPurpose purpose, size_t length)
    {
        stageFor(purpose).provider().commit(length);
    }

    IoAction ReceiveFlow::deliver(KiottyNetSpan data, const Caller& caller)
    {
        const KiottyNetConnectionAction next = caller.handler.dispatch(caller, readEvent(data));
        _receive.consumer().commit(data.length);
        return caller.admit(next);
    }

    ReceiveFlow::RefillResult ReceiveFlow::refill()
    {
        _receive.compact();
        size_t produced = 0;
        const bool secure = _tls.status().is_active;
        if (secure)
        {
            const DecodeResult decoded = _tls.decode(_receive);
            if (!decoded) { return RefillResult::failure(decoded.code); }
            produced = decoded.value;
        }
        const size_t writable = _receive.provider().writable();
        const bool is_stuck = produced == 0 && writable == 0;
        if (is_stuck) { return RefillResult::failure(KIOTTY_NET_CODE_REQUEST_TOO_LARGE); }
        return RefillResult::success(produced);
    }

    IoAction ReceiveFlow::refuse(KiottyNetCode code, const Caller& caller)
    {
        const TlsFailure failed = _tls.status().failure;
        const bool is_full = code == KIOTTY_NET_CODE_REQUEST_TOO_LARGE;
        const char* message = is_full ? STAGE_FULL_MESSAGE : failed.message;
        caller.reject(code, message);
        return IoAction::close(code);
    }

    StageBase& ReceiveFlow::stageFor(IoPurpose purpose)
    {
        const bool secure = _tls.status().is_active;
        if (purpose == IoPurpose::HANDSHAKE || secure)
        {
            return _tls.stages().receive;
        }
        return _receive;
    }

    namespace
    {
        bool isRead(const IoAction& action)
        {
            return action.kind == IoActionKind::READ || action.kind == IoActionKind::READ_UNTIL;
        }

        IoAction settled(const IoAction& action)
        {
            switch (action.kind)
            {
            case IoActionKind::HOLD:
            case IoActionKind::START_WORK:
            case IoActionKind::CLOSE:
            case IoActionKind::READ:
            case IoActionKind::READ_UNTIL:
                return action;
            case IoActionKind::NONE:
            case IoActionKind::WRITE:
            case IoActionKind::ACCEPT:
                return IoAction::none();
            }
            return IoAction::none();
        }

        ReadyResult satisfied(IoAction& action, StageBase& stage)
        {
            if (action.kind == IoActionKind::READ)
            {
                return exactly(action, stage);
            }
            return until(action, stage);
        }

        ReadyResult exactly(const IoAction& action, StageBase& stage)
        {
            const size_t exact = action.as.read.exact;
            const bool is_too_large = exact > stage.capacity();
            if (is_too_large) { return ReadyResult::failure(KIOTTY_NET_CODE_REQUEST_TOO_LARGE); }
            StageBase::Consumer in = stage.consumer();
            const size_t readable = in.readable();
            if (readable < exact)
            {
                return nothingYet();
            }
            return ReadyResult::success(KiottyNetSpan{ in.peek(), exact });
        }

        ReadyResult until(IoAction& action, StageBase& stage)
        {
            const size_t length = action.as.read_until.length;
            const size_t max = action.as.read_until.max;
            if (length == 0) { return ReadyResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            StageBase::Consumer in = stage.consumer();
            const size_t readable = in.readable();
            const Span<const char> scanned{ in.peek(), readable < max ? readable : max };
            const size_t position = findDelimiter(action, scanned);
            if (position + length <= scanned.length)
            {
                return ReadyResult::success(KiottyNetSpan{ scanned.data, position + length });
            }
            action.as.read_until.scanned = position;
            if (readable >= max) { return ReadyResult::failure(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
            return nothingYet();
        }

        size_t findDelimiter(const IoAction& action, Span<const char> scanned)
        {
            const size_t length = action.as.read_until.length;
            size_t position = action.as.read_until.scanned;
            while (position + length <= scanned.length)
            {
                const char* candidate = scanned.data + position;
                const char* delimiter = action.as.read_until.delimiter;
                const int compared = std::memcmp(candidate, delimiter, length);
                if (compared == 0)
                {
                    return position;
                }
                position += 1;
            }
            return position;
        }

        ReadyResult nothingYet()
        {
            return ReadyResult::success(KiottyNetSpan{ nullptr, 0 });
        }
    }
}
