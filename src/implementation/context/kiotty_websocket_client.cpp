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
        constexpr uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t MIX_FIRST = 0xBF58476D1CE4E5B9ULL;
        constexpr uint64_t MIX_SECOND = 0x94D049BB133111EBULL;
        constexpr uint32_t BITS_PER_BYTE = 8;

        using KeyText = FixedString<WEB_SOCKET_KEY_LENGTH + 1>;

        template <size_t N>
        Span<const char> literalOf(const char (&text)[N])
        {
            return Span<const char>{ text, N - 1 };
        }

        uint64_t mixOf(uint64_t state);
        KeyText  randomKey();
        bool     appendHost(UpgradeText& text, const KiottyNetAddress& address, bool is_secure);
        bool     appendPort(UpgradeText& text, uint16_t port);
    }

    WebSocketClient::WebSocketClient()
        : EventClient()
        , _site()
        , _path()
        , _request()
        , _accept()
        , _scratch()
        , _push_mutex()
        , _opened()
        , _mask_state(0)
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
        _path = configuration.path;
        _site.limits = limits;
        _site.receive_capacity = configuration.client.receive_capacity;
        _site.send_capacity = configuration.client.send_capacity;
        _site.socket_owner = this;
        _site.lane = this;
        _site.role = SocketRole::CLIENT;
        _site.registry.initialize(1);
        _scratch.resize(configuration.client.push_capacity);
        std::random_device seed;
        _mask_state.store((static_cast<uint64_t>(seed()) << 32U) | seed());
        return _site.sessions.initialize(_site, 1, limits);
    }

    void WebSocketClient::reset() noexcept
    {
        EventClient::reset();
        _is_ended.store(false);
    }

    KiottyNetCode WebSocketClient::run(const ClientRun& request)
    {
        const KiottyNetWebSocketHandler& handler = request.handler;
        const bool has_handler = handler.onOpen != nullptr && handler.onMessage != nullptr;
        if (!has_handler) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
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
        const MaskKey key = nextMaskKey();
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

    KiottyNetCode WebSocketClient::prepareUpgrade(const KiottyNetAddress& address, bool is_secure)
    {
        const KeyText key = randomKey();
        _request.clear();
        bool is_written = _request.append(literalOf("GET "));
        is_written = is_written && _request.append(_path.span());
        is_written = is_written && _request.append(literalOf(" HTTP/1.1\r\nHost: "));
        is_written = is_written && appendHost(_request, address, is_secure);
        is_written = is_written && _request.append(literalOf("\r\nUpgrade: websocket\r\n"));
        is_written = is_written && _request.append(literalOf("Connection: Upgrade\r\n"));
        is_written = is_written && _request.append(literalOf("Sec-WebSocket-Key: "));
        is_written = is_written && _request.append(key.span());
        is_written = is_written && _request.append(literalOf("\r\nSec-WebSocket-Version: 13"));
        is_written = is_written && _request.append(literalOf("\r\n\r\n"));
        if (!is_written) { return KIOTTY_NET_CODE_INVALID_ARGUMENT; }
        _accept = acceptOf(key.span());
        _site.upgrade.request = _request.span();
        _site.upgrade.accept = _accept.span();
        return KIOTTY_NET_CODE_SUCCESS;
    }

    KiottyNetCode WebSocketClient::runOnce(const ClientRun& request)
    {
        const auto begin = [this]() {
            _result = KIOTTY_NET_CODE_SUCCESS;
            _is_ended.store(false);
        };
        _opened.unpark(begin);
        const bool is_secure = request.tls != nullptr;
        const KiottyNetCode prepared = prepareUpgrade(request.address, is_secure);
        if (prepared != KIOTTY_NET_CODE_SUCCESS) { return prepared; }
        const WebSocketTarget target{ request.handler, request.app_context, NO_SOCKET_PATH };
        _site.upgrade.target = target;
        _site.upgrade.opened = &_opened;
        _site.upgrade.failure = KIOTTY_NET_CODE_SUCCESS;
        const KiottyNetStatefulAppContext stateful = webSocketClientStatefulOf(_site);
        const StatefulHandler handler(stateful, *this);
        const KiottyNetCode code = EventClient::run(handler, request.address, request.tls);
        const KiottyNetCode failure = _site.upgrade.failure;
        const bool is_refused = failure != KIOTTY_NET_CODE_SUCCESS
                                && failure != KIOTTY_NET_CODE_PEER_CLOSED;
        const bool is_dropped = code == KIOTTY_NET_CODE_PEER_CLOSED;
        return is_dropped && is_refused ? failure : code;
    }

    MaskKey WebSocketClient::nextMaskKey()
    {
        const uint64_t mixed = mixOf(_mask_state.fetch_add(GOLDEN_GAMMA) + GOLDEN_GAMMA);
        MaskKey key{};
        for (size_t i = 0; i < MASK_KEY_BYTES; ++i)
        {
            key.bytes[i] = static_cast<uint8_t>(mixed >> (BITS_PER_BYTE * i));
        }
        return key;
    }

    bool WebSocketClient::isOpen()
    {
        return _site.sessions.links()[0].socket.load() != 0;
    }

    namespace
    {
        uint64_t mixOf(uint64_t state)
        {
            uint64_t mixed = (state ^ (state >> 30U)) * MIX_FIRST;
            mixed = (mixed ^ (mixed >> 27U)) * MIX_SECOND;
            return mixed ^ (mixed >> 31U);
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
