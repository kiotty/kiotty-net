#include "data/repository/connection/kiotty_adopt_action.h"

#include "data/repository/connection/kiotty_sender.h"

namespace kiotty::net::implementation
{
    namespace
    {
        AdoptResult adoptRead(const KiottyNetConnectionAction& returned);
        AdoptResult adoptReadUntil(const KiottyNetConnectionAction& returned);
        AdoptResult adoptWrite(const KiottyNetConnectionAction& returned, StageBase& send);
        bool        isValidDelimiter(KiottyNetSpan delimiter, size_t max);
        bool        isValidWrite(KiottyNetSpan data);
    }

    AdoptResult adoptAction(const KiottyNetConnectionAction& returned, StageBase& send)
    {
        switch (returned.type)
        {
        case KIOTTY_NET_ACTION_TYPE_HOLD:
        case KIOTTY_NET_ACTION_TYPE_START_WORK:
        case KIOTTY_NET_ACTION_TYPE_CLOSE:
            return AdoptResult::success(IoAction::from(returned));
        case KIOTTY_NET_ACTION_TYPE_READ:
            return adoptRead(returned);
        case KIOTTY_NET_ACTION_TYPE_READ_UNTIL:
            return adoptReadUntil(returned);
        case KIOTTY_NET_ACTION_TYPE_WRITE:
            return adoptWrite(returned, send);
        }
        return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT);
    }

    namespace
    {
        AdoptResult adoptRead(const KiottyNetConnectionAction& returned)
        {
            const bool is_empty = returned.exact == 0;
            if (is_empty) { return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            return AdoptResult::success(IoAction::from(returned));
        }

        AdoptResult adoptReadUntil(const KiottyNetConnectionAction& returned)
        {
            const KiottyNetSpan delimiter = returned.delimiter;
            const bool valid = isValidDelimiter(delimiter, returned.max);
            if (!valid) { return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            return AdoptResult::success(IoAction::from(returned));
        }

        AdoptResult adoptWrite(const KiottyNetConnectionAction& returned, StageBase& send)
        {
            const bool valid = isValidWrite(returned.data);
            if (!valid) { return AdoptResult::failure(KIOTTY_NET_CODE_INVALID_ARGUMENT); }
            const KiottyNetCode staged = Sender::stage(send, returned.data);
            if (staged != KIOTTY_NET_CODE_SUCCESS) { return AdoptResult::failure(staged); }
            return AdoptResult::success(IoAction::hold(returned.timeout_milliseconds));
        }

        bool isValidDelimiter(KiottyNetSpan delimiter, size_t max)
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

        bool isValidWrite(KiottyNetSpan data)
        {
            return data.data != nullptr && data.length > 0;
        }
    }
}
