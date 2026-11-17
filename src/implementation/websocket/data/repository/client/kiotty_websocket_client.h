#pragma once

#include "base/data/repository/endpoint/kiotty_event_client.h"
#include "core/kiotty_fixed_string.h"
#include "core/kiotty_parker.h"
#include "core/kiotty_result.h"
#include "http/data/repository/server/kiotty_http_session_host.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/endpoint/kiotty_websocket_endpoint.h"
#include "websocket/data/repository/send/kiotty_websocket_lane.h"
#include "websocket/data/repository/server/kiotty_websocket_handler.h"
#include "websocket/domain/entity/kiotty_websocket_handshake.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kiotty::net::implementation
{
    inline constexpr size_t MAX_UPGRADE_REQUEST_BYTES = 1024;

    using SocketPath = FixedString<KIOTTY_NET_MAX_URL_LENGTH>;
    using UpgradeText = FixedString<MAX_UPGRADE_REQUEST_BYTES>;
    using RegistrationResult = Result<HttpSessionHost&, KiottyNetCode>;

    struct WebSocketClientConfiguration
    {
        KiottyNetClientConfiguration client;
        KiottyNetHttpLimits          limits;
    };

    struct ClientRun
    {
        const KiottyNetAddress&                address;
        Span<const char>                       path;
        const KiottyNetTlsClientConfiguration* tls;
        KiottyNetAppContext                    app_context;
    };

    class WebSocketClient : public EventClient,
                            public WebSocketHandler,
                            public PushLane,
                            public KiottyNetWebSocketEndpoint
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
        RegistrationResult       registration();
        KiottyNetCode            setProtocols(Span<const char> protocols);
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
        KiottyNetCode prepareUpgrade(const ClientRun& request);
        KiottyNetCode runOnce(const ClientRun& request);
        void          reportUnopened(KiottyNetCode code);
        bool          isOpen();

        HttpSessionHost       _host;
        ProtocolText          _protocols;
        UpgradeText           _request;
        AcceptText            _accept;
        std::vector<char>     _scratch;
        std::mutex            _push_mutex;
        Parker                _opened;
        std::atomic<bool>     _is_running;
        std::atomic<bool>     _is_ended;
        KiottyNetCode         _result;
    };
}
