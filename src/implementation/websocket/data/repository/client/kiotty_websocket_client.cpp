#include "websocket/data/repository/client/kiotty_websocket_client.h"

#include "core/kiotty_base64.h"
#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/request/kiotty_http_request_head_builder.h"
#include "http/data/repository/server/kiotty_http_configuration.h"
#include "statemachine/data/repository/kiotty_stateful_handler.h"
#include "websocket/data/repository/client/kiotty_websocket_client_machine.h"
#include "websocket/data/repository/frame/kiotty_websocket_frame_builder.h"
#include "websocket/data/repository/frame/kiotty_websocket_mask_rules.h"
#include "websocket/data/repository/handshake/kiotty_websocket_key_rules.h"
#include "websocket/data/repository/server/kiotty_websocket_access.h"

#include <random>

namespace kiotty::net::implementation
{
    namespace
    {
        WebSocketAccess clientAccessOf(HttpSessionHost& host, WebSocketHandler& handler)
        {
            return WebSocketAccess(host.registry, handler, host.push_capacity);
        }

        constexpr size_t   KEY_BYTES = 16;
        const char* const  REFUSED_MESSAGE = "websocket upgrade refused";
        const char* const  TIMEOUT_MESSAGE = "websocket upgrade timed out";
        const char* const  UNOPENED_MESSAGE = "websocket closed before open";

        using KeyText = FixedString<WEB_SOCKET_KEY_LENGTH + 1>;

        KeyText     randomKey();
        const char* messageOf(KiottyNetCode code);
        bool        isReported(KiottyNetCode code);
        bool        appendHost(UpgradeText& text, const ClientRun& request);
    }

    WebSocketClient::WebSocketClient()
        : EventClient()
        , WebSocketHandler()
        , _host()
        , _protocols()
        , _request()
        , _accept()
        , _scratch()
        , _push_mutex()
        , _opened()
        , _is_running(false)
        , _is_ended(false)
        , _result(KIOTTY_NET_CODE_SUCCESS)
    {
    }

    WebSocketClient::~WebSocketClient() = default;

    KiottyNetCode WebSocketClient::initialize(PoolHandle id,
                                              const WebSocketClientConfiguration& configuration)
    {
        const KiottyNetHttpLimits& limits = configuration.limits;
        if (!isInitialized(limits)) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode base = EventClient::initialize(id, configuration.client);
        if (base != KIOTTY_NET_CODE_SUCCESS) { return base; }
        _protocols.clear();
        _host.limits = limits;
        _host.receive_capacity = configuration.client.receive_capacity;
        _host.send_capacity = configuration.client.send_capacity;
        _host.push_capacity = configuration.client.push_capacity;
        _host.upgrade = this;
        _host.registry.initialize(1);
        WebSocketHandler::initialize(1, 0, SocketRole::CLIENT);
        owner = this;
        lane = this;
        _scratch.resize(configuration.client.push_capacity);
        std::random_device seed;
        mask_state.store((static_cast<uint64_t>(seed()) << 32U) | seed());
        client.events = ClientEvents{ nullptr, nullptr, nullptr };
        _host.notices.clear();
        return _host.sessions.initialize(_host, 1, limits);
    }

    void WebSocketClient::reset() noexcept
    {
        EventClient::reset();
        _is_ended.store(false);
    }

    RegistrationResult WebSocketClient::registration()
    {
        if (_is_running.load()) { return RegistrationResult::failure(KIOTTY_NET_CODE_BUSY); }
        return RegistrationResult::success(_host);
    }

    KiottyNetCode WebSocketClient::setProtocols(Span<const char> protocols)
    {
        if (_is_running.load()) { return KIOTTY_NET_CODE_BUSY; }
        const bool is_stored = _protocols.assign(protocols);
        if (!is_stored) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode WebSocketClient::run(const ClientRun& request)
    {
        const bool has_message = client.events.on_message != nullptr;
        if (!has_message) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        bool is_idle = false;
        const bool is_first = _is_running.compare_exchange_strong(is_idle, true);
        if (!is_first) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const KiottyNetCode code = runOnce(request);
        const auto finish = [this, code]() {
            _result = code;
            _is_ended.store(true);
        };
        _opened.unpark(finish);
        _is_running.store(false);
        return code;
    }

    KiottyNetCode WebSocketClient::await(uint64_t timeout_milliseconds)
    {
        KiottyNetCode answer = KIOTTY_NET_CODE_TIMEOUT;
        const auto settled = [this, &answer]() {
            if (isOpen()) { answer = KIOTTY_NET_CODE_SUCCESS; }
            else if (_is_ended.load()) { answer = _result; }
            return answer != KIOTTY_NET_CODE_TIMEOUT;
        };
        (void)_opened.park(settled, timeout_milliseconds);
        return answer;
    }

    KiottyNetWebSocketHandle WebSocketClient::socket()
    {
        const int64_t id = links()[0].socket.load();
        if (id == 0) { return KiottyNetWebSocketHandle{ nullptr, 0 }; }
        return KiottyNetWebSocketHandle{ this, id };
    }

    KiottyNetCode WebSocketClient::push(int64_t connection,
                                        FrameOpcode opcode,
                                        Span<const char> payload)
    {
        const std::lock_guard<std::mutex> lock(_push_mutex);
        const MaskKey key = nextMaskKey(mask_state);
        const FrameHead head = buildMaskedFrameHead(opcode, payload.length, key);
        const size_t total = head.length + payload.length;
        if (total > _scratch.size()) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Span<char> frame{ _scratch.data(), total };
        (void)frame.copyFrom(headBytesOf(head));
        const Span<char> body = frame.skip(head.length);
        (void)body.copyFrom(payload);
        applyMask(body, key, 0);
        return EventClient::notify(connection, KiottyNetSpan{ frame.data, frame.length });
    }

    KiottyNetWebSocketInfo WebSocketClient::socketInfo(int64_t id)
    {
        return clientAccessOf(_host, *this).info(id);
    }

    KiottyNetSpan WebSocketClient::upgradeHeader(int64_t id, Span<const char> name)
    {
        return clientAccessOf(_host, *this).header(id, name);
    }

    KiottyNetCode WebSocketClient::selectProtocol(int64_t id, Span<const char> protocol)
    {
        return clientAccessOf(_host, *this).select(id, protocol);
    }

    KiottyNetCode WebSocketClient::sendSocket(int64_t id,
                                              KiottyNetWebSocketOpcode opcode,
                                              KiottyNetSpan data)
    {
        return clientAccessOf(_host, *this).send(id, opcode, data);
    }

    KiottyNetCode WebSocketClient::closeSocket(int64_t id, uint16_t status, Span<const char> reason)
    {
        return clientAccessOf(_host, *this).close(id, status, reason);
    }

    KiottyNetCode WebSocketClient::prepareUpgrade(const ClientRun& request)
    {
        const KeyText key = randomKey();
        const bool has_protocols = !_protocols.empty();
        _request.clear();
        bool is_written = _request.append(literalOf("GET "));
        is_written = is_written && _request.append(request.path);
        is_written = is_written && _request.append(literalOf(" HTTP/1.1\r\nHost: "));
        is_written = is_written && appendHost(_request, request);
        is_written = is_written && _request.append(literalOf("\r\nUpgrade: websocket\r\n"));
        is_written = is_written && _request.append(literalOf("Connection: Upgrade\r\n"));
        is_written = is_written && _request.append(literalOf("Sec-WebSocket-Key: "));
        is_written = is_written && _request.append(key.span());
        is_written = is_written && _request.append(literalOf("\r\nSec-WebSocket-Version: 13"));
        if (has_protocols)
        {
            is_written = is_written && _request.append(literalOf("\r\nSec-WebSocket-Protocol: "));
            is_written = is_written && _request.append(_protocols.span());
        }
        is_written = is_written && _request.append(literalOf("\r\n\r\n"));
        if (!is_written) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _accept = acceptOf(key.span());
        client.request = _request.span();
        client.accept = _accept.span();
        client.protocols = _protocols.span();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode WebSocketClient::runOnce(const ClientRun& request)
    {
        const auto begin = [this]() {
            _result = KIOTTY_NET_CODE_SUCCESS;
            _is_ended.store(false);
        };
        _opened.unpark(begin);
        const KiottyNetCode prepared = prepareUpgrade(request);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return prepared; }
        _host.app_context = request.app_context;
        client.peer = KiottyNetConnectionInfo{};
        client.opened = &_opened;
        client.failure = KIOTTY_NET_CODE_SUCCESS;
        client.has_opened = false;
        client.has_failed = false;
        const KiottyNetStatefulAppContext stateful = webSocketClientStatefulOf(_host);
        const StatefulHandler handler(stateful, *this);
        const KiottyNetCode code = EventClient::run(handler, request.address, request.tls);
        const KiottyNetCode failure = client.failure;
        const bool is_refused = failure != KIOTTY_NET_CODE_SUCCESS
                                && failure != KIOTTY_NET_CODE_PEER_CLOSED;
        const bool is_dropped = code == KIOTTY_NET_CODE_PEER_CLOSED;
        const KiottyNetCode result = is_dropped && is_refused ? failure : code;
        reportUnopened(result);
        return result;
    }

    void WebSocketClient::reportUnopened(KiottyNetCode code)
    {
        const bool is_settled = client.has_opened || client.has_failed;
        if (is_settled || !isReported(code)) { return; }
        client.has_failed = true;
        _host.notices.reportError(_host.app_context, client.peer, code, messageOf(code));
    }

    bool WebSocketClient::isOpen()
    {
        return links()[0].socket.load() != 0;
    }

    namespace
    {
        const char* messageOf(KiottyNetCode code)
        {
            if (code == KIOTTY_NET_CODE_PROTOCOL_ERROR) { return REFUSED_MESSAGE; }
            if (code == KIOTTY_NET_CODE_TIMEOUT) { return TIMEOUT_MESSAGE; }
            return UNOPENED_MESSAGE;
        }

        bool isReported(KiottyNetCode code)
        {
            const bool is_quiet = code == KIOTTY_NET_CODE_SUCCESS
                                  || code == KIOTTY_NET_CODE_CLOSED
                                  || code == KIOTTY_NET_CODE_INVALID_ARGUMENT;
            return !is_quiet;
        }

        KeyText randomKey()
        {
            std::random_device source;
            uint8_t bytes[KEY_BYTES] = {};
            for (uint8_t& byte : bytes) { byte = static_cast<uint8_t>(source()); }
            char encoded[WEB_SOCKET_KEY_LENGTH] = {};
            const Span<const uint8_t> input{ bytes, KEY_BYTES };
            (void)encodeBase64(input, Span<char>{ encoded, WEB_SOCKET_KEY_LENGTH });
            KeyText key;
            (void)key.assign(Span<const char>{ encoded, WEB_SOCKET_KEY_LENGTH });
            return key;
        }

        bool appendHost(UpgradeText& text, const ClientRun& request)
        {
            const bool is_secure = request.tls != nullptr;
            const uint16_t usual = is_secure ? HTTPS_DEFAULT_PORT : HTTP_DEFAULT_PORT;
            const HostFieldResult host = hostFieldOf(request.address, usual);
            if (!host) { return false; }
            return text.append(host.value.span());
        }
    }
}
