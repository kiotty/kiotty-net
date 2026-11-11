#include "domain/entity/kiotty_io_action.h"

#include "core/kiotty_span.h"

namespace kiotty::net::implementation
{
    namespace
    {
        IoAction blank(IoActionKind kind, IoPurpose purpose, uint32_t timeout_milliseconds);
        bool     fitsDelimiter(KiottyNetSpan delimiter);
    }

    IoAction IoAction::none()
    {
        return blank(IoActionKind::NONE, IoPurpose::USER, 0);
    }

    IoAction IoAction::hold(uint32_t timeout_milliseconds)
    {
        return blank(IoActionKind::HOLD, IoPurpose::USER, timeout_milliseconds);
    }

    IoAction IoAction::read(IoPurpose purpose, uint32_t timeout_milliseconds, size_t exact)
    {
        IoAction action = blank(IoActionKind::READ, purpose, timeout_milliseconds);
        action.as.read.exact = exact;
        return action;
    }

    IoAction IoAction::readUntil(uint32_t timeout_milliseconds, KiottyNetSpan delimiter, size_t max)
    {
        IoAction action = blank(IoActionKind::READ_UNTIL, IoPurpose::USER, timeout_milliseconds);
        action.as.read_until.max = max;
        if (!fitsDelimiter(delimiter)) { return action; }
        const Span<char> target{ action.as.read_until.delimiter, MAX_DELIMITER_LENGTH };
        const Span<const char> source{ delimiter.data, delimiter.length };
        const bool copied = target.copyFrom(source);
        if (!copied) { return action; }
        action.as.read_until.length = static_cast<uint8_t>(delimiter.length);
        return action;
    }

    IoAction IoAction::write(IoPurpose purpose, uint32_t timeout_milliseconds, size_t length)
    {
        IoAction action = blank(IoActionKind::WRITE, purpose, timeout_milliseconds);
        action.as.write.length = length;
        return action;
    }

    IoAction IoAction::startWork()
    {
        return blank(IoActionKind::START_WORK, IoPurpose::USER, 0);
    }

    IoAction IoAction::close(KiottyNetCode reason)
    {
        IoAction action = blank(IoActionKind::CLOSE, IoPurpose::USER, 0);
        action.as.close.reason = reason;
        return action;
    }

    IoAction IoAction::accept()
    {
        return blank(IoActionKind::ACCEPT, IoPurpose::USER, 0);
    }

    IoAction IoAction::from(const KiottyNetConnectionAction& action)
    {
        switch (action.type)
        {
        case KIOTTY_NET_ACTION_TYPE_HOLD:
            return hold(action.timeout_milliseconds);
        case KIOTTY_NET_ACTION_TYPE_READ:
            return read(IoPurpose::USER, action.timeout_milliseconds, action.exact);
        case KIOTTY_NET_ACTION_TYPE_READ_UNTIL:
            return readUntil(action.timeout_milliseconds,
                             action.delimiter,
                             action.max);
        case KIOTTY_NET_ACTION_TYPE_WRITE:
            return write(IoPurpose::USER, action.timeout_milliseconds, action.data.length);
        case KIOTTY_NET_ACTION_TYPE_START_WORK:
            return startWork();
        case KIOTTY_NET_ACTION_TYPE_CLOSE:
            return close(action.reason);
        }
        return none();
    }

    namespace
    {
        IoAction blank(IoActionKind kind, IoPurpose purpose, uint32_t timeout_milliseconds)
        {
            IoAction action{};
            action.kind = kind;
            action.purpose = purpose;
            action.timeout_milliseconds = timeout_milliseconds;
            return action;
        }

        bool fitsDelimiter(KiottyNetSpan delimiter)
        {
            if (delimiter.data == nullptr) { return false; }
            return delimiter.length > 0 && delimiter.length <= IoAction::MAX_DELIMITER_LENGTH;
        }
    }
}
