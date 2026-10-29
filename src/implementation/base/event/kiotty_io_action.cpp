#include "base/event/kiotty_io_action.h"

#include <cstring>

namespace kiotty
{
    namespace net
    {
        const size_t IoAction::MAX_DELIMITER_LENGTH;

        namespace
        {
            IoAction blank(IoActionKind kind, IoPurpose purpose, uint32_t timeout_ms)
            {
                IoAction action;
                std::memset(&action, 0, sizeof(action));
                action.kind = kind;
                action.purpose = purpose;
                action.timeout_ms = timeout_ms;
                return action;
            }

            bool fitsDelimiter(KiottyNetSpan delimiter)
            {
                if (delimiter.data == nullptr)
                {
                    return false;
                }
                return delimiter.length > 0 && delimiter.length <= IoAction::MAX_DELIMITER_LENGTH;
            }
        }

        IoAction IoAction::none()
        {
            return blank(IoActionKind::NONE, IoPurpose::USER, 0);
        }

        IoAction IoAction::hold(uint32_t timeout_ms)
        {
            return blank(IoActionKind::HOLD, IoPurpose::USER, timeout_ms);
        }

        IoAction IoAction::read(IoPurpose purpose, uint32_t timeout_ms, size_t exact)
        {
            IoAction action = blank(IoActionKind::READ, purpose, timeout_ms);
            action.as.read.exact = exact;
            return action;
        }

        IoAction IoAction::readUntil(uint32_t timeout_ms, KiottyNetSpan delimiter, size_t max)
        {
            IoAction action = blank(IoActionKind::READ_UNTIL, IoPurpose::USER, timeout_ms);
            action.as.read_until.max = max;
            if (!fitsDelimiter(delimiter))
            {
                return action;
            }
            std::memcpy(action.as.read_until.delimiter, delimiter.data, delimiter.length);
            action.as.read_until.length = static_cast<uint8_t>(delimiter.length);
            return action;
        }

        IoAction IoAction::write(IoPurpose purpose, uint32_t timeout_ms)
        {
            return blank(IoActionKind::WRITE, purpose, timeout_ms);
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
            case KIOTTY_ACTION_HOLD:
                return hold(action.timeout_ms);
            case KIOTTY_ACTION_READ:
                return read(IoPurpose::USER, action.timeout_ms, action.as.read.exact);
            case KIOTTY_ACTION_READ_UNTIL:
                return readUntil(action.timeout_ms, action.as.read_until.delimiter, action.as.read_until.max);
            case KIOTTY_ACTION_WRITE:
                return write(IoPurpose::USER, action.timeout_ms);
            case KIOTTY_ACTION_START_WORK:
                return startWork();
            case KIOTTY_ACTION_CLOSE:
                return close(action.as.close.reason);
            }
            return none();
        }
    }
}
