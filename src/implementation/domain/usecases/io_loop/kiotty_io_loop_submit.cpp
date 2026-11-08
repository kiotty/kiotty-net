#include "domain/usecases/io_loop/kiotty_io_loop.h"

#include "data/repository/connection/kiotty_connection.h"

namespace kiotty::net::implementation
{
    SocketCode IoLoop::submit(IoSource& source, const IoAction& action, EventListener& listener)
    {
        switch (action.kind)
        {
        case IoActionKind::NONE:
        case IoActionKind::HOLD:
        case IoActionKind::START_WORK:
            return SocketCode::SUCCESS;
        case IoActionKind::CLOSE:
            source.detach(listener);
            source.shutdown();
            return SocketCode::SUCCESS;
        case IoActionKind::READ:
        case IoActionKind::READ_UNTIL:
        case IoActionKind::WRITE:
        case IoActionKind::ACCEPT:
            return listener.submit(source.eventFor(action), action, source.resolve(action));
        }
        return SocketCode::SUCCESS;
    }

    void IoLoop::armAccept(const LoopParts& parts)
    {
        const SocketCode armed = submit(parts.acceptor, IoAction::accept(), parts.listener);
        if (armed != SocketCode::SUCCESS)
        {
            reportAcceptFailure(toNetCode(armed), parts);
        }
    }
}
