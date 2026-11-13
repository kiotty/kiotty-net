#include "context/kiotty_websocket_client.h"

#include "context/kiotty_http_configuration.h"
#include "core/kiotty_base64.h"
#include "core/kiotty_text.h"
#include "domain/usecases/http/kiotty_http_token.h"
#include "domain/usecases/statemachine/kiotty_stateful_handler.h"
#include "domain/usecases/websocket/kiotty_websocket_access.h"
#include "domain/usecases/websocket/kiotty_websocket_client_machine.h"

#include <random>

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t   KEY_BYTES = 16;
        constexpr uint16_t HTTP_PORT = 80;
        constexpr uint16_t HTTPS_PORT = 443;
        constexpr size_t   PORT_DIGITS = 5;
        constexpr uint16_t DECIMAL = 10;
        const char* const  REFUSED_MESSAGE = "websocket upgrade refused";
        const char* const  TIMEOUT_MESSAGE = "websocket upgrade timed out";
        const char* const  UNOPENED_MESSAGE = "websocket closed before open";

        using KeyText = FixedString<WEB_SOCKET_KEY_LENGTH + 1>;

        template <size_t N>
        Span<const char> literalOf(const char (&text)[N])
        {
            return Span<const char>{ text, N - 1 };
        }

        KeyText     randomKey();
        const char* messageOf(KiottyNetCode code);
        bool        isReported(KiottyNetCode code);
        bool     appendHost(UpgradeText& text, const KiottyNetAddress& address, bool is_secure);
        bool     appendPort(UpgradeText& text, uint16_t port);
    }

    WebSocketClient::WebSocketClient()
        : EventClient()
        , _site()
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
        _site.limits = limits;
        _site.receive_capacity = configuration.client.receive_capacity;
        _site.send_capacity = configuration.client.send_capacity;
        _site.socket_owner = this;
        _site.lane = this;
        _site.role = SocketRole::CLIENT;
        _site.registry.initialize(1);
        _scratch.resize(configuration.client.push_capacity);
        std::random_device seed;
        _site.mask_state.store((static_cast<uint64_t>(seed()) << 32U) | seed());
        _site.upgrade.events = ClientEvents{ nullptr, nullptr, nullptr };
        _site.notices.clear();
        return _site.sessions.initialize(_site, 1, limits);
    }

    void WebSocketClient::reset() noexcept
    {
        EventClient::reset();
        _is_ended.store(false);
    }

    RegistrationResult WebSocketClient::registration()
    {
        if (_is_running.load()) { return RegistrationResult::failure(KIOTTY_NET_CODE_BUSY); }
        return RegistrationResult::success(_site);
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
        const bool has_message = _site.upgrade.events.on_message != nullptr;
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
        const int64_t id = _site.sessions.links()[0].socket.load();
        if (id == 0) { return KiottyNetWebSocketHandle{ nullptr, 0 }; }
        return KiottyNetWebSocketHandle{ this, id };
    }

    KiottyNetCode WebSocketClient::push(int64_t connection,
                                        FrameOpcode opcode,
                                        Span<const char> payload)
    {
        const std::lock_guard<std::mutex> lock(_push_mutex);
        const MaskKey key = nextMaskKey(_site.mask_state);
        const FrameHead head = buildMaskedFrameHead(opcode, payload.length, key);
        const size_t total = head.length + payload.length;
        if (total > _scratch.size()) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        const Span<char> frame{ _scratch.data(), total };
        (void)frame.copyFrom(head.span());
        const Span<char> body = frame.skip(head.length);
        (void)body.copyFrom(payload);
        applyMask(body, key, 0);
        return EventClient::notify(connection, KiottyNetSpan{ frame.data, frame.length });
    }

    KiottyNetWebSocketInfo WebSocketClient::socketInfo(int64_t id)
    {
        return WebSocketAccess(_site).info(id);
    }

    KiottyNetSpan WebSocketClient::upgradeHeader(int64_t id, Span<const char> name)
    {
        return WebSocketAccess(_site).header(id, name);
    }

    KiottyNetCode WebSocketClient::selectProtocol(int64_t id, Span<const char> protocol)
    {
        return WebSocketAccess(_site).select(id, protocol);
    }

    KiottyNetCode WebSocketClient::sendSocket(int64_t id,
                                              KiottyNetWebSocketOpcode opcode,
                                              KiottyNetSpan data)
    {
        return WebSocketAccess(_site).send(id, opcode, data);
    }

    KiottyNetCode WebSocketClient::closeSocket(int64_t id, uint16_t status, Span<const char> reason)
    {
        return WebSocketAccess(_site).close(id, status, reason);
    }

    KiottyNetCode WebSocketClient::prepareUpgrade(const ClientRun& request)
    {
        const bool is_secure = request.tls != nullptr;
        const KeyText key = randomKey();
        const bool has_protocols = !_protocols.empty();
        _request.clear();
        bool is_written = _request.append(literalOf("GET "));
        is_written = is_written && _request.append(request.path);
        is_written = is_written && _request.append(literalOf(" HTTP/1.1\r\nHost: "));
        is_written = is_written && appendHost(_request, request.address, is_secure);
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
        _site.upgrade.request = _request.span();
        _site.upgrade.accept = _accept.span();
        _site.upgrade.protocols = _protocols.span();
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
        _site.app_context = request.app_context;
        _site.upgrade.peer = KiottyNetConnectionInfo{};
        _site.upgrade.opened = &_opened;
        _site.upgrade.failure = KIOTTY_NET_CODE_SUCCESS;
        _site.upgrade.has_opened = false;
        _site.upgrade.has_failed = false;
        const KiottyNetStatefulAppContext stateful = webSocketClientStatefulOf(_site);
        const StatefulHandler handler(stateful, *this);
        const KiottyNetCode code = EventClient::run(handler, request.address, request.tls);
        const KiottyNetCode failure = _site.upgrade.failure;
        const bool is_refused = failure != KIOTTY_NET_CODE_SUCCESS
                                && failure != KIOTTY_NET_CODE_PEER_CLOSED;
        const bool is_dropped = code == KIOTTY_NET_CODE_PEER_CLOSED;
        const KiottyNetCode result = is_dropped && is_refused ? failure : code;
        reportUnopened(result);
        return result;
    }

    void WebSocketClient::reportUnopened(KiottyNetCode code)
    {
        const ClientUpgrade& upgrade = _site.upgrade;
        const bool is_settled = upgrade.has_opened || upgrade.has_failed;
        if (is_settled || !isReported(code)) { return; }
        _site.upgrade.has_failed = true;
        _site.notices.reportError(_site.app_context, upgrade.peer, code, messageOf(code));
    }

    bool WebSocketClient::isOpen()
    {
        return _site.sessions.links()[0].socket.load() != 0;
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

        bool appendHost(UpgradeText& text, const KiottyNetAddress& address, bool is_secure)
        {
            const Span<const char> host = textOf(address.host);
            const bool is_literal_v6 = findCharacter(host, ':', 0) < host.length;
            bool is_written = !is_literal_v6 || text.append(literalOf("["));
            is_written = is_written && text.append(host);
            is_written = is_written && (!is_literal_v6 || text.append(literalOf("]")));
            const uint16_t usual = is_secure ? HTTPS_PORT : HTTP_PORT;
            if (address.port == usual) { return is_written; }
            return is_written && text.append(literalOf(":")) && appendPort(text, address.port);
        }

        bool appendPort(UpgradeText& text, uint16_t port)
        {
            char digits[PORT_DIGITS] = {};
            size_t count = 0;
            uint16_t rest = port;
            do
            {
                digits[PORT_DIGITS - 1 - count] = static_cast<char>('0' + rest % DECIMAL);
                rest = static_cast<uint16_t>(rest / DECIMAL);
                count += 1;
            } while (rest != 0 && count < PORT_DIGITS);
            return text.append(Span<const char>{ digits + PORT_DIGITS - count, count });
        }
    }
}
