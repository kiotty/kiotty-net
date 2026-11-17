#include "websocket/data/repository/client/kiotty_websocket_client_machine.h"

#include "base/domain/entity/kiotty_action.h"
#include "core/kiotty_clock.h"
#include "http/data/repository/machine/kiotty_http_close.h"
#include "http/data/repository/machine/kiotty_http_rule_names.h"
#include "http/data/repository/response/kiotty_http_status_line_parser.h"
#include "websocket/data/repository/control/kiotty_websocket_control.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_reader.h"
#include "websocket/data/repository/handshake/kiotty_websocket_opening.h"
#include "websocket/data/repository/handshake/kiotty_websocket_upgrade.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"

#include <cstdint>
#include <limits>

namespace kiotty::net::implementation
{
    namespace
    {
        using Next = KiottyNet_Transition<HttpState>;
        using Event = KiottyNetConnectionEvent;
        using Session = KiottyNetConnectionContext;
        using Handle = KiottyNetConnectionHandle;

        constexpr char          PROTOCOL_HEADER[] = "Sec-WebSocket-Protocol";
        constexpr size_t        PROTOCOL_HEADER_LENGTH = sizeof(PROTOCOL_HEADER) - 1;
        constexpr char          REPLY_LINE_END[] = "\r\n";
        constexpr KiottyNetSpan REPLY_LINE_END_SPAN = { REPLY_LINE_END, 2 };

        KiottyNetTransition onClientConnected(Session context, Handle connection, Event event);
        KiottyNetTransition onUpgradeSent(Session context, Handle connection, Event event);
        KiottyNetTransition onStatusLine(Session context, Handle connection, Event event);
        KiottyNetTransition onResponseHeader(Session context, Handle connection, Event event);
        KiottyNetTransition onClientFrame(Session context, Handle connection, Event event);
        KiottyNetTransition onClientTimeout(Session context, Handle connection, Event event);
        KiottyNetTransition onClientClosed(Session context, Handle connection, Event event);

        WebSocketClientState&      clientStateOf(Session context);
        KiottyNetTransition        writeRequest(WebSocketClientState& state);
        KiottyNetTransition        readReplyLine(WebSocketClientState& state, HttpState next);
        KiottyNetTransition        openClient(WebSocketClientState& state);
        SocketState&               clientSocketOf(WebSocketClientState& state);
        Span<const char>           replyLineOf(KiottyNetSpan data);
        uint32_t                   replyTimeLeft(const WebSocketClientState& state);
        int32_t                    nextClientGeneration(int32_t generation);
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
                .to(HEADER_LINE, UPGRADED, CLOSING),
            KiottyNet_from(UPGRADED).when(READ).then(onClientFrame).to(UPGRADED, CLOSING),
            KiottyNet_from(UPGRADED).when(TIMEOUT).then(onClientFrame).to(UPGRADED, CLOSING),
            KiottyNet_when(TIMEOUT).then(onClientTimeout).to(CLOSING),
            KiottyNet_when(CLOSED).then(onClientClosed));

        constexpr KiottyNetStateMachine CLIENT_MACHINE =
            KiottyNet_makeStateMachine(CLIENT_RULES, UPGRADE_SENDING);
    }

    KiottyNetStatefulAppContext webSocketClientStatefulOf(WebSocketClientState& state)
    {
        return KiottyNetStatefulAppContext{
            &state, &CLIENT_MACHINE, createClientSession, releaseClientSession, reportClientError,
        };
    }

    namespace
    {
        KiottyNetTransition onClientConnected(Session context, Handle connection, Event)
        {
            WebSocketClientState& state = clientStateOf(context);
            state.id.generation = nextClientGeneration(state.id.generation);
            state.request = KiottyNetHttpRequest{};
            state.request.connection = connection;
            state.reply.begin();
            state.response.begin();
            clientSocketOf(state).reset();
            state.sent = 0;
            const uint32_t timeout = state.limits.header_timeout_milliseconds;
            state.header_deadline = timeout == 0 ? 0 : monotonicMilliseconds() + timeout;
            return writeRequest(state);
        }

        KiottyNetTransition onUpgradeSent(Session context, Handle, Event event)
        {
            WebSocketClientState& state = clientStateOf(context);
            state.sent += event.length;
            const bool has_more = state.sent < state.upgrade.request.length;
            if (has_more) { return writeRequest(state); }
            return readReplyLine(state, STATUS_LINE);
        }

        KiottyNetTransition onStatusLine(Session context, Handle, Event event)
        {
            WebSocketClientState& state = clientStateOf(context);
            const bool is_switching = isSwitchingLine(replyLineOf(event.data));
            if (!is_switching) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
            return readReplyLine(state, HEADER_LINE);
        }

        KiottyNetTransition onResponseHeader(Session context, Handle, Event event)
        {
            WebSocketClientState& state = clientStateOf(context);
            const Span<const char> line = replyLineOf(event.data);
            if (line.empty()) { return openClient(state); }
            const KiottyNetCode stored = state.reply.takeHeader(line);
            const bool is_stored = stored == KIOTTY_NET_CODE_SUCCESS;
            if (!is_stored) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
            return readReplyLine(state, HEADER_LINE);
        }

        KiottyNetTransition onClientFrame(Session context, Handle, Event event)
        {
            WebSocketClientState& state = clientStateOf(context);
            return stepFrame(*state.handler, clientUpgradeOf(state), event);
        }

        KiottyNetTransition onClientTimeout(Session, Handle, Event)
        {
            return closeWith(KIOTTY_NET_CODE_TIMEOUT);
        }

        KiottyNetTransition onClientClosed(Session context, Handle, Event event)
        {
            WebSocketClientState& state = clientStateOf(context);
            ClientUpgrade& upgrade = state.upgrade;
            if (!clientSocketOf(state).is_open) { upgrade.failure = event.reason; }
            endSocket(*state.handler, clientUpgradeOf(state), event.reason);
            return Next::moveToNextState(CLOSING, thenHold(0));
        }

        WebSocketClientState& clientStateOf(Session context)
        {
            return *static_cast<WebSocketClientState*>(context);
        }

        KiottyNetTransition writeRequest(WebSocketClientState& state)
        {
            const Span<const char> whole = state.upgrade.request;
            const Span<const char> request = whole.skip(state.sent);
            const KiottyNetSpan data{ request.data, request.length };
            const uint32_t timeout = state.limits.header_timeout_milliseconds;
            return Next::moveToNextState(UPGRADE_SENDING, thenWrite(data, timeout));
        }

        KiottyNetTransition readReplyLine(WebSocketClientState& state, HttpState next)
        {
            const size_t header_line = state.limits.max_header_bytes + REPLY_LINE_END_SPAN.length;
            const size_t capacity = state.receive_capacity;
            const size_t limit = capacity < header_line ? capacity : header_line;
            const uint32_t timeout = replyTimeLeft(state);
            const KiottyNetConnectionAction action =
                thenReadUntil(REPLY_LINE_END_SPAN, limit, timeout);
            return Next::moveToNextState(next, action);
        }

        KiottyNetTransition openClient(WebSocketClientState& state)
        {
            const Span<const KiottyNetHttpHeader> headers = state.reply.headers();
            const ClientUpgrade& upgrade = state.upgrade;
            const bool is_upgraded = isUpgradeResponse(headers, upgrade.accept, upgrade.protocols);
            if (!is_upgraded) { return closeWith(KIOTTY_NET_CODE_PROTOCOL_ERROR); }
            const Span<const char> name{ PROTOCOL_HEADER, PROTOCOL_HEADER_LENGTH };
            const Span<const char> chosen = headerOf(headers, name);
            (void)clientSocketOf(state).protocol.assign(chosen);
            return openSocket(*state.handler, clientUpgradeOf(state));
        }

        SocketState& clientSocketOf(WebSocketClientState& state)
        {
            return state.handler->socketAt(state.id.index);
        }

        Span<const char> replyLineOf(KiottyNetSpan data)
        {
            const bool has_line_end = data.length >= REPLY_LINE_END_SPAN.length;
            const size_t length =
                has_line_end ? data.length - REPLY_LINE_END_SPAN.length : data.length;
            return Span<const char>{ data.data, length };
        }

        uint32_t replyTimeLeft(const WebSocketClientState& state)
        {
            if (state.header_deadline == 0) { return 0; }
            const uint64_t now = monotonicMilliseconds();
            if (now >= state.header_deadline) { return EXPIRED_MILLISECONDS; }
            const uint64_t left = state.header_deadline - now;
            const uint64_t largest = std::numeric_limits<uint32_t>::max();
            return static_cast<uint32_t>(left < largest ? left : largest);
        }

        int32_t nextClientGeneration(int32_t generation)
        {
            if (generation == INT32_MAX) { return 1; }
            return generation + 1;
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
            WebSocketClientState& state = *static_cast<WebSocketClientState*>(app_context);
            state.upgrade.peer = info;
            state.info = info;
            return &state;
        }

        void releaseClientSession(KiottyNetAppContext, KiottyNetConnectionContext)
        {
        }

        void reportClientError(KiottyNetAppContext app_context,
                               KiottyNetConnectionInfo info,
                               KiottyNetCode code,
                               const char* message)
        {
            WebSocketClientState& state = *static_cast<WebSocketClientState*>(app_context);
            ClientUpgrade& upgrade = state.upgrade;
            const bool is_settled = upgrade.has_opened || upgrade.has_failed;
            if (is_settled) { return; }
            upgrade.has_failed = true;
            if (state.on_error == nullptr) { return; }
            state.on_error(state.app_context, info, code, message);
        }
    }
}
