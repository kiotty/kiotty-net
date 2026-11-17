#include "websocket/data/repository/client/kiotty_websocket_client_machine.h"

#include "base/domain/entity/kiotty_action.h"
#include "http/data/repository/machine/kiotty_http_exchange.h"
#include "http/data/repository/machine/kiotty_http_rule_names.h"
#include "http/data/repository/response/kiotty_http_status_line_parser.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/handshake/kiotty_websocket_opening.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Event = KiottyNetConnectionEvent;
        using Session = KiottyNetConnectionContext;
        using Handle = KiottyNetConnectionHandle;

        constexpr char   PROTOCOL_HEADER[] = "Sec-WebSocket-Protocol";
        constexpr size_t PROTOCOL_HEADER_LENGTH = sizeof(PROTOCOL_HEADER) - 1;

        KiottyNetTransition onClientConnected(Session context, Handle connection, Event event);
        KiottyNetTransition onUpgradeSent(Session context, Handle connection, Event event);
        KiottyNetTransition onStatusLine(Session context, Handle connection, Event event);
        KiottyNetTransition onResponseHeader(Session context, Handle connection, Event event);
        KiottyNetTransition onClientTimeout(Session context, Handle connection, Event event);
        KiottyNetTransition onClientClosed(Session context, Handle connection, Event event);

        KiottyNetTransition        writeRequest(HttpSession& session);
        bool                       isSwitchingLine(Span<const char> line);
        KiottyNetConnectionContext createClientSession(KiottyNetAppContext app_context,
                                                       KiottyNetConnectionInfo info);
        void                       releaseClientSession(KiottyNetAppContext app_context,
                                                        KiottyNetConnectionContext context);
        void                       reportClientError(KiottyNetAppContext app_context,
                                                     KiottyNetConnectionInfo info,
                                                     KiottyNetCode code,
                                                     const char* message);

        constexpr HttpState UPGRADE_SENDING = HttpState::SENDING;
        constexpr HttpState STATUS_LINE = HttpState::REQUEST_LINE;

        constexpr auto CLIENT_RULES = KiottyNet_rules(
            KiottyNet_when(CONNECTED).then(onClientConnected).to(UPGRADE_SENDING),
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
                .to(FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, FRAME_START, CLOSING),
            KiottyNet_from(FRAME_LENGTH)
                .when(READ)
                .then(onFrameLength)
                .to(FRAME_MASK, FRAME_PAYLOAD, FRAME_START, CLOSING),
            KiottyNet_from(FRAME_MASK)
                .when(READ)
                .then(onFrameMask)
                .to(FRAME_PAYLOAD, FRAME_START, CLOSING),
            KiottyNet_from(FRAME_PAYLOAD)
                .when(READ)
                .then(onFramePayload)
                .to(FRAME_PAYLOAD, FRAME_START, CLOSING),
            KiottyNet_from(FRAME_START)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, CLOSING),
            KiottyNet_from(FRAME_LENGTH)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, CLOSING),
            KiottyNet_from(FRAME_MASK)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, CLOSING),
            KiottyNet_from(FRAME_PAYLOAD)
                .when(TIMEOUT)
                .then(onSocketTimeout)
                .to(FRAME_START, FRAME_LENGTH, FRAME_MASK, FRAME_PAYLOAD, CLOSING),
            KiottyNet_when(TIMEOUT).then(onClientTimeout).to(CLOSING),
            KiottyNet_when(CLOSED).then(onClientClosed));

        constexpr KiottyNetStateMachine CLIENT_MACHINE =
            KiottyNet_makeStateMachine(CLIENT_RULES, UPGRADE_SENDING);
    }

    KiottyNetStatefulAppContext webSocketClientStatefulOf(HttpSite& site)
    {
        return KiottyNetStatefulAppContext{
            &site, &CLIENT_MACHINE, createClientSession, releaseClientSession, reportClientError,
        };
    }

    namespace
    {
        KiottyNetTransition onClientConnected(Session context, Handle connection, Event)
        {
            HttpSession& session = sessionOf(context);
            advanceSession(session);
            session.request.begin(connection);
            session.response.begin();
            socketOf(session).reset();
            session.sent = 0;
            startHeaderClock(session);
            return writeRequest(session);
        }

        KiottyNetTransition onUpgradeSent(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            session.sent += event.length;
            const bool has_more = session.sent < handlerOf(session).client.request.length;
            if (has_more) { return writeRequest(session); }
            return readLine(session, STATUS_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onStatusLine(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            const bool is_switching = isSwitchingLine(lineOf(event.data));
            if (!is_switching) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onResponseHeader(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            const Span<const char> line = lineOf(event.data);
            const KiottyNetCode broken = KIOTTY_NET_CODE_PROTOCOL_ERROR;
            if (line.empty())
            {
                const KiottyNetHttpRequest& response = session.request.request();
                const ClientUpgrade& upgrade = handlerOf(session).client;
                const bool is_upgraded =
                    isUpgradeResponse(response, upgrade.accept, upgrade.protocols);
                if (!is_upgraded) { return closeWith(broken); }
                const Span<const char> name{ PROTOCOL_HEADER, PROTOCOL_HEADER_LENGTH };
                const Span<const char> chosen = headerOf(response, name);
                (void)socketOf(session).protocol.assign(chosen);
                return openSocket(session);
            }
            const KiottyNetHttpStatus status = session.request.takeHeader(line);
            if (status != NO_ERROR_STATUS) { return closeWith(broken); }
            return readLine(session, HEADER_LINE, headerTimeLeft(session));
        }

        KiottyNetTransition onClientTimeout(Session, Handle, Event)
        {
            return closeWith(KIOTTY_NET_CODE_TIMEOUT);
        }

        KiottyNetTransition onClientClosed(Session context, Handle, Event event)
        {
            HttpSession& session = sessionOf(context);
            ClientUpgrade& upgrade = handlerOf(session).client;
            if (!socketOf(session).is_open) { upgrade.failure = event.reason; }
            endSocket(session, event.reason);
            return Next::moveToNextState(CLOSING, thenHold(0));
        }

        KiottyNetTransition writeRequest(HttpSession& session)
        {
            const Span<const char> request = handlerOf(session).client.request.skip(session.sent);
            const KiottyNetSpan data{ request.data, request.length };
            const uint32_t timeout = session.site.limits.header_timeout_milliseconds;
            return Next::moveToNextState(UPGRADE_SENDING, thenWrite(data, timeout));
        }

        bool isSwitchingLine(Span<const char> line)
        {
            const StatusLineResult parsed = parseStatusLine(line);
            if (!parsed) { return false; }
            const bool is_one_one = parsed.value.version == HttpVersion::V11;
            return is_one_one && parsed.value.status == KIOTTY_NET_HTTP_STATUS_SWITCHING_PROTOCOLS;
        }

        KiottyNetConnectionContext createClientSession(KiottyNetAppContext app_context,
                                                       KiottyNetConnectionInfo info)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            handlerOf(site).client.peer = info;
            const SessionResult acquired = site.sessions.acquire(info);
            if (!acquired) { return nullptr; }
            return &acquired.value();
        }

        void releaseClientSession(KiottyNetAppContext app_context,
                                  KiottyNetConnectionContext context)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            site.sessions.release(sessionOf(context));
        }

        void reportClientError(KiottyNetAppContext app_context,
                               KiottyNetConnectionInfo info,
                               KiottyNetCode code,
                               const char* message)
        {
            HttpSite& site = *static_cast<HttpSite*>(app_context);
            ClientUpgrade& upgrade = handlerOf(site).client;
            const bool is_settled = upgrade.has_opened || upgrade.has_failed;
            if (is_settled) { return; }
            upgrade.has_failed = true;
            site.notices.reportError(site.app_context, info, code, message);
        }
    }
}
