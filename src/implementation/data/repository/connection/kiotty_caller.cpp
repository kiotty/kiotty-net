#include "data/repository/connection/kiotty_caller.h"

#include "data/repository/connection/kiotty_adopt_action.h"

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const INVALID_ACTION_MESSAGE = "action rejected: invalid argument";
    }

    KiottyNetConnectionHandle Caller::handle() const
    {
        return KiottyNetConnectionHandle{ &handler.owner, id.value() };
    }

    KiottyNetConnectionStream Caller::stream() const
    {
        return KiottyNetConnectionStream{ &handler.owner, id.value() };
    }

    void Caller::reject(KiottyNetCode code, const char* message) const
    {
        handler.callbacks.onError(handler.app_context, info, code, message);
    }

    IoAction Caller::admit(const KiottyNetConnectionAction& returned) const
    {
        const AdoptResult adopted = adoptAction(returned, send);
        if (adopted)
        {
            return adopted.value;
        }
        if (adopted.code == KIOTTY_NET_CODE_INVALID_ARGUMENT)
        {
            reject(KIOTTY_NET_CODE_INVALID_ARGUMENT, INVALID_ACTION_MESSAGE);
        }
        return IoAction::close(adopted.code);
    }
}
