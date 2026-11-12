#pragma once

#include "context/kiotty_event_client.h"
#include "context/kiotty_websocket_endpoint.h"
#include "core/kiotty_fixed_string.h"
#include "core/kiotty_parker.h"
#include "data/repository/http/kiotty_http_site.h"
#include "domain/usecases/websocket/kiotty_websocket_handshake.h"
#include "kiotty_net_websocket.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kiotty::net::implementation
{
    inline constexpr size_t MAX_UPGRADE_REQUEST_BYTES = 768;

    using SocketPath = FixedString<KIOTTY_NET_MAX_URL_LENGTH>;
    using UpgradeText = FixedString<MAX_UPGRADE_REQUEST_BYTES>;

    struct WebSocketClientConfiguration
    {
        KiottyNetClientConfiguration client;
        KiottyNetHttpLimits          limits;
        SocketPath                   path;
    };

    struct ClientRun
    {
        const KiottyNetAddress&                address;
        const KiottyNetTlsClientConfiguration* tls;
        const KiottyNetWebSocketHandler&       handler;
        KiottyNetAppContext                    app_context;
    };

    class WebSocketClient : public EventClient, public PushLane, public KiottyNetWebSocketEndpoint
    {
    public:
        using configuration_type = WebSocketClientConfiguration;

        WebSocketClient();
        ~WebSocketClient() override;

        WebSocketClient(const WebSocketClient&) = delete;
        WebSocketClient& operator=(const WebSocketClient&) = delete;
        WebSocketClient(WebSocketClient&&) = delete;
        WebSocketClient& operator=(WebSocketClient&&) = delete;

        KiottyNetCode            initialize(PoolHandle id,
                                            const WebSocketClientConfiguration& configuration);
        void                     reset() noexcept;
        KiottyNetCode            run(const ClientRun& request);
        KiottyNetCode            await(uint64_t timeout_milliseconds);
        KiottyNetWebSocketHandle socket();

        KiottyNetCode          push(int64_t connection,
                                    FrameOpcode opcode,
                                    Span<const char> payload) override;
        KiottyNetWebSocketInfo socketInfo(int64_t id) override;
        KiottyNetSpan          upgradeHeader(int64_t id, Span<const char> name) override;
        KiottyNetCode          selectProtocol(int64_t id, Span<const char> protocol) override;
        KiottyNetCode          sendSocket(int64_t id,
                                          KiottyNetWebSocketOpcode opcode,
                                          KiottyNetSpan data) override;
        KiottyNetCode          closeSocket(int64_t id,
                                           uint16_t status,
                                           Span<const char> reason) override;

    private:
        KiottyNetCode prepareUpgrade(const KiottyNetAddress& address, bool is_secure);
        KiottyNetCode runOnce(const ClientRun& request);
        MaskKey       nextMaskKey();
        bool          isOpen();

        HttpSite              _site;
        SocketPath            _path;
        UpgradeText           _request;
        AcceptText            _accept;
        std::vector<char>     _scratch;
        std::mutex            _push_mutex;
        Parker                _opened;
        std::atomic<uint64_t> _mask_state;
        std::atomic<bool>     _is_running;
        std::atomic<bool>     _is_ended;
        KiottyNetCode         _result;
    };
}
