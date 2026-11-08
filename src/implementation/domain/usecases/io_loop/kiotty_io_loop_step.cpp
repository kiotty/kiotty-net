#include "domain/usecases/io_loop/kiotty_io_loop.h"

#include "core/kiotty_clock.h"
#include "data/repository/connection/kiotty_connection.h"
#include "domain/entity/kiotty_socket_code.h"

#include <utility>

namespace kiotty::net::implementation
{
    namespace
    {
        const char* const ACCEPT_FAILED_MESSAGE = "accept failed. retrying";
        const char* const TABLE_FULL_MESSAGE = "accept rejected: connection table is full";
        const char* const OPEN_FAILED_MESSAGE = "connection open failed";
    }

    void IoLoop::stepUntilQuiet(const LoopParts& parts)
    {
        bool stepped = true;
        while (stepped)
        {
            stepped = stepLocalEvents(parts);
        }
    }

    bool IoLoop::stepLocalEvents(const LoopParts& parts)
    {
        bool stepped = false;
        const uint64_t now = monotonicMilliseconds();
        for (int32_t i = 0; i < parts.host.capacity(); ++i)
        {
            const bool slot_stepped = stepSlot(parts, i, now);
            stepped = stepped || slot_stepped;
        }
        return stepped;
    }

    bool IoLoop::stepSlot(const LoopParts& parts, int32_t slot, uint64_t now)
    {
        Connection& connection = parts.host.at(slot);
        bool stepped = false;
        if (connection.status().isExpired(now))
        {
            step(IoCompletion::timeout(slot), parts);
            stepped = true;
        }
        if (connection.flows().work.isReady())
        {
            step(IoCompletion::workReady(slot), parts);
            stepped = true;
        }
        if (connection.flows().work.isFinished())
        {
            const IoAction outcome = connection.flows().work.takeOutcome();
            step(IoCompletion::workDone(slot, outcome), parts);
            stepped = true;
        }
        return stepped;
    }

    void IoLoop::step(const IoCompletion& completion, const LoopParts& parts)
    {
        switch (completion.kind)
        {
        case IoKind::WAKE:
            return;
        case IoKind::ACCEPT:
            access(completion, parts);
            return;
        case IoKind::RECEIVE:
        case IoKind::SEND:
        case IoKind::TIMEOUT:
        case IoKind::WORK_READY:
        case IoKind::WORK_DONE:
        {
            Connection& connection = parts.host.at(completion.slot);
            const IoAction action =
                connection.dispatch(completion, parts.handler, parts.host.notificationMutex());
            submitTo(connection, action, parts);
            return;
        }
        }
    }

    void IoLoop::access(const IoCompletion& completion, const LoopParts& parts)
    {
        if (completion.code == SocketCode::CANCELLED)
        {
            return;
        }
        if (completion.code != SocketCode::SUCCESS)
        {
            reportAcceptFailure(toNetCode(completion.code), parts);
            return;
        }
        ActiveSocketResult accepted = parts.acceptor.accept();
        if (!accepted)
        {
            reportAcceptFailure(toNetCode(accepted.code), parts);
            return;
        }
        admitAccepted(std::move(accepted.value), parts);
    }

    void IoLoop::admitAccepted(ActiveSocket&& socket, const LoopParts& parts)
    {
        const KiottyNetConnectionInfo info = socket.peer();
        if (_stopped.load())
        {
            return;
        }
        if (parts.host.full())
        {
            parts.handler.callbacks.onError(parts.handler.app_context, info,
                                            KIOTTY_NET_CODE_TABLE_FULL, TABLE_FULL_MESSAGE);
            return;
        }
        const KiottyNetConnectionDecision decision =
            parts.handler.callbacks.onConnected(parts.handler.app_context, info);
        if (!decision.accepted)
        {
            return;
        }
        ConnectionConfiguration configuration = { std::move(socket),
                                                  ConnectionLimits{ 0, 0, 0 },
                                                  decision.connection_context,
                                                  "",
                                                  parts.profile };
        open(std::move(configuration), parts);
    }

    void IoLoop::open(ConnectionConfiguration&& configuration, const LoopParts& parts)
    {
        const KiottyNetConnectionInfo info = configuration.socket.peer();
        ConnectionResult acquired = parts.host.acquire(std::move(configuration));
        if (!acquired)
        {
            parts.handler.callbacks.onError(parts.handler.app_context, info,
                                            acquired.code, OPEN_FAILED_MESSAGE);
            return;
        }
        Connection& connection = acquired.value();
        const SocketCode attached = connection.attach(parts.listener);
        if (attached != SocketCode::SUCCESS)
        {
            submitTo(connection, connection.close(toNetCode(attached), parts.handler), parts);
            return;
        }
        submitTo(connection, connection.onOpened(parts.handler), parts);
    }

    void IoLoop::submitTo(Connection& connection, const IoAction& action, const LoopParts& parts)
    {
        const SocketCode submitted = submit(connection, action, parts.listener);
        if (submitted == SocketCode::SUCCESS)
        {
            return;
        }
        const IoAction closing = connection.close(KIOTTY_NET_CODE_SOCKET_ERROR, parts.handler);
        submit(connection, closing, parts.listener);
    }

    void IoLoop::reportAcceptFailure(KiottyNetCode code, const LoopParts& parts)
    {
        _accept_retry_at = monotonicMilliseconds() + ACCEPT_RETRY_MILLISECONDS;
        parts.handler.callbacks.onError(parts.handler.app_context, KiottyNetConnectionInfo{},
                                        code, ACCEPT_FAILED_MESSAGE);
    }
}
