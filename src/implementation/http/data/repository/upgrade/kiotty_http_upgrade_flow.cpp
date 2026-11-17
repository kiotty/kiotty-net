#include "http/data/repository/upgrade/kiotty_http_upgrade_flow.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/datasource/kiotty_http_session_pool.h"
#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/body/kiotty_http_message_framing_parser.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "http/data/repository/server/kiotty_http_session_host.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;

        KiottyNetTransition sendSwitching(HttpSession& session, KiottyNetConnectionEvent event);
        KiottyNetTransition openUpgraded(HttpSession& session);
    }

    UpgradeSession upgradeSessionOf(HttpSession& session)
    {
        const HttpSessionHost& host = session.host;
        const bool has_body = hasBody(session.state.framing.body);
        return UpgradeSession{
            session.request.request(),
            session.response,
            session.request.bodyArea(),
            host.limits,
            host.receive_capacity,
            host.app_context,
            session.id,
            has_body,
        };
    }

    KiottyNetTransition dispatchUpgrade(HttpSession& session,
                                        HttpUpgradeHandler& upgrade,
                                        int32_t route)
    {
        session.state.phase = SessionPhase::UPGRADING;
        const UpgradeAnswer answer = upgrade.answer(upgradeSessionOf(session), route);
        if (answer == UpgradeAnswer::DROP) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
        if (answer == UpgradeAnswer::REFUSE)
        {
            session.state.persistence = Persistence::CLOSE;
            return startSending(session);
        }
        session.state.persistence = Persistence::UPGRADE;
        advanceSession(session);
        const KiottyNetTransition sending = startSending(session);
        return Next::moveToNextState(HttpState::UPGRADED, sending.entry_action);
    }

    KiottyNetTransition stepUpgraded(HttpSession& session, KiottyNetConnectionEvent event)
    {
        const bool is_socket = session.state.phase == SessionPhase::SOCKET;
        if (!is_socket) { return sendSwitching(session, event); }
        return session.host.upgrade->step(upgradeSessionOf(session), event);
    }

    namespace
    {
        KiottyNetTransition sendSwitching(HttpSession& session, KiottyNetConnectionEvent event)
        {
            const KiottyNetConnectionEventType type = event.type;
            const bool is_timeout = type == KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT;
            if (is_timeout) { return closeWith(KIOTTY_NET_CODE_TIMEOUT); }
            const bool is_written = type == KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN;
            if (!is_written) { return closeWith(KIOTTY_NET_CODE_UNHANDLED_EVENT); }
            session.state.sent += event.length;
            const PieceResult piece = pieceAt(session, session.state.sent);
            if (!piece) { return closeWith(piece.code); }
            if (piece.value.empty()) { return openUpgraded(session); }
            const KiottyNetSpan data{ piece.value.data, piece.value.length };
            return Next::moveToNextState(HttpState::UPGRADED, thenWrite(data, 0));
        }

        KiottyNetTransition openUpgraded(HttpSession& session)
        {
            const KiottyNetConnectionHandle connection = session.request.request().connection;
            session.request.begin(connection);
            session.state.phase = SessionPhase::SOCKET;
            return session.host.upgrade->open(upgradeSessionOf(session));
        }
    }
}
