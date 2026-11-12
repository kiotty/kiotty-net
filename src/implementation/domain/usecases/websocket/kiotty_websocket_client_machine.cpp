#include "domain/usecases/websocket/kiotty_websocket_client_machine.h"

#include "domain/entity/kiotty_action.h"
#include "domain/usecases/http/kiotty_http_exchange.h"
#include "domain/usecases/websocket/kiotty_websocket_rows.h"
#include "domain/usecases/websocket/kiotty_websocket_upgrade.h"

#include <cstring>

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Event = KiottyNetConnectionEvent;
        using Context = KiottyNetConnectionContext;
        using Connection = KiottyNetConnectionHandle;
        using EventType = KiottyNetConnectionEventType;

        constexpr char   SWITCHING_LINE[] = "HTTP/1.1 101";
        constexpr size_t SWITCHING_LENGTH = sizeof(SWITCHING_LINE) - 1;

        KiottyNetTransition onConnected(Context context, Connection connection, Event event);
        KiottyNetTransition onUpgradeSent(Context context, Connection connection, Event event);
        KiottyNetTransition onStatusLine(Context context, Connection connection, Event event);
        KiottyNetTransition onResponseHeader(Context context, Connection connection, Event event);
        KiottyNetTransition onTimeout(Context context, Connection connection, Event event);
        KiottyNetTransition onClosed(Context context, Connection connection, Event event);

        KiottyNetTransition        writeRequest(HttpSession& session);
        bool                       isSwitchingLine(Span<const char> line);
        KiottyNetConnectionContext createSession(KiottyNetAppContext app_context,
                                                 KiottyNetConnectionInfo info);
        void                       releaseSession(KiottyNetAppContext app_context,
                                                  KiottyNetConnectionContext context);
        void                       reportError(KiottyNetAppContext app_context,
                                               KiottyNetConnectionInfo info,
                                               KiottyNetCode code,
                                               const char* message);

        constexpr HttpState UPGRADE_SENDING = HttpState::SENDING;
        constexpr HttpState STATUS_LINE = HttpState::REQUEST_LINE;
        constexpr HttpState HEADER_LINE = HttpState::HEADER_LINE;
        constexpr HttpState CLOSING = HttpState::CLOSING;
        constexpr HttpState FRAME_START = HttpState::FRAME_START;
        constexpr HttpState FRAME_LENGTH = HttpState::FRAME_LENGTH;
        constexpr HttpState FRAME_MASK = HttpState::FRAME_MASK;
        constexpr HttpState FRAME_PAYLOAD = HttpState::FRAME_PAYLOAD;
        constexpr HttpState SOCKET_CLOSING = HttpState::SOCKET_CLOSING;

        constexpr EventType CONNECTED = KIOTTY_NET_CONNECTION_EVENT_TYPE_CONNECTED;
        constexpr EventType READ = KIOTTY_NET_CONNECTION_EVENT_TYPE_READ;
        constexpr EventType WRITTEN = KIOTTY_NET_CONNECTION_EVENT_TYPE_WRITTEN;
        constexpr EventType TIMEOUT = KIOTTY_NET_CONNECTION_EVENT_TYPE_TIMEOUT;
        constexpr EventType CLOSED = KIOTTY_NET_CONNECTION_EVENT_TYPE_CLOSED;

        constexpr auto CLIENT_RULES = KiottyNet_rules(
            KiottyNet_when(CONNECTED).then(onConnected).to(UPGRADE_SENDING),
            KiottyNet_from(UPGRADE_SENDING)
                .when(WRITTEN)
                .then(onUpgradeSent)
                .to(UPGRADE_SENDING, STATUS_LINE),
            KiottyNet_from(STATUS_LINE).when(READ).then(onStatusLine).to(HEADER_LINE, CLOSING),
            KiottyNet_from(HEADER_LINE)
                .when(READ)
                .then(onResponseHeader)
                .to(HEADER_LINE, FRAME_START, CLOSING),
            KiottyNet_from(FRAME_START)
                .when(READ)
                .then(onFrameStart)
                .to(FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_LENGTH)
                .when(READ)
                .then(onFrameLength)
                .to(FRAME_MASK, FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_MASK)
                .when(READ)
                .then(onFrameMask)
                .to(FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_PAYLOAD)
                .when(READ)
                .then(onFramePayload)
                .to(FRAME_PAYLOAD, FRAME_START, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_START)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_LENGTH)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_MASK)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(FRAME_PAYLOAD)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, SOCKET_CLOSING, CLOSING),
            KiottyNet_from(SOCKET_CLOSING).when(WRITTEN).then(onCloseWritten).to(CLOSING),
            KiottyNet_when(TIMEOUT).then(onTimeout).to(CLOSING),
            KiottyNet_when(CLOSED).then(onClosed));

        constexpr KiottyNetStateMachine CLIENT_MACHINE =
            KiottyNet_makeStateMachine(CLIENT_RULES, UPGRADE_SENDING);
    }

    KiottyNetStatefulAppContext webSocketClientStatefulOf(HttpSite& site)
    {
        return KiottyNetStatefulAppContext{
            &site, &CLIENT_MACHINE, createSession, releaseSession, reportError,
        };
    }

    namespace
    {
        KiottyNetTransition onConnected(Context context, Connection connection, Event)
        {
            HttpSession& session = sessionOf(context);
            session.id.generation = nextSessionGeneration(session.id.generation);
            session.request.begin(connection);
            session.response.begin();
            session.socket.reset();
            session.socket.target = session.site.upgrade.target;
            session.sent = 0;
            startHeaderClock(session);
            return writeRequest(session);
        }

        KiottyNetTransition onUpgradeSent(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            session.sent += event.length;
            const bool has_more = session.sent < session.site.upgrade.request.length;
            if (has_more) { return writeRequest(session); }
            return readLine(session, STATUS_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onStatusLine(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            const bool is_switching = isSwitchingLine(lineOf(event.data));
            if (!is_switching) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onResponseHeader(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            const Span<const char> line = lineOf(event.data);
            const KiottyNetCode broken = KIOTTY_NET_CODE_PROTOCOL_ERROR;
            if (line.empty())
            {
                const KiottyNetHttpRequest& response = session.request.request();
                const bool is_upgraded = isUpgradeResponse(response, session.site.upgrade.accept);
                if (!is_upgraded) { return closeWith(broken); }
                return openSocket(session);
            }
            const KiottyNetHttpStatus status = session.request.takeHeader(line);
            if (status != NO_ERROR_STATUS) { return closeWith(broken); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onTimeout(Context, Connection, Event)
        {
            return closeWith(KIOTTY_NET_CODE_TIMEOUT);
        }

        KiottyNetTransition onClosed(Context context, Connection, Event event)
        {
            HttpSession& session = sessionOf(context);
            if (!session.socket.is_open) { session.site.upgrade.failure = event.reason; }
            endSocket(session, event.reason);
            return Next::moveToNextState(CLOSING, thenHold(0));
        }

        KiottyNetTransition writeRequest(HttpSession& session)
        {
            const Span<const char> request = session.site.upgrade.request.skip(session.sent);
            const KiottyNetSpan data{ request.data, request.length };
            const uint32_t timeout = session.site.limits.header_timeout_milliseconds;
            return Next::moveToNextState(UPGRADE_SENDING, thenWrite(data, timeout));
        }

        bool isSwitchingLine(Span<const char> line)
        {
            if (line.length < SWITCHING_LENGTH) { return false; }
            if (std::memcmp(line.data, SWITCHING_LINE, SWITCHING_LENGTH) != 0) { return false; }
            return line.length == SWITCHING_LENGTH || line[SWITCHING_LENGTH] == ' ';
        }

        KiottyNetConnectionContext createSession(KiottyNetAppContext app_context,
                                                 KiottyNetConnectionInfo info)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            const SessionResult acquired = site.sessions.acquire(info);
            if (!acquired) { return nullptr; }
            return &acquired.value();
        }

        void releaseSession(KiottyNetAppContext app_context, KiottyNetConnectionContext context)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            site.sessions.release(sessionOf(context));
        }

        void reportError(KiottyNetAppContext app_context,
                         KiottyNetConnectionInfo info,
                         KiottyNetCode code,
                         const char* message)
        {
            const HttpSite& site = *static_cast<HttpSite*>(app_context);
            site.notices.reportError(info, code, message);
        }
    }
}
