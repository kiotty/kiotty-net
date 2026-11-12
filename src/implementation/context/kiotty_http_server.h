#pragma once

#include "context/kiotty_http_configuration.h"
#include "context/kiotty_http_work.h"
#include "context/kiotty_server.h"
#include "context/kiotty_websocket_endpoint.h"
#include "core/kiotty_result.h"
#include "data/repository/http/kiotty_http_site.h"
#include "kiotty_net_http.h"

#include <atomic>
#include <cstdint>

namespace kiotty::net::implementation
{
    using RegistryResult = Result<HttpRegistry&, KiottyNetCode>;
    using NoticesResult = Result<HttpNotices&, KiottyNetCode>;

    class HttpServer : public Server, public PushLane, public KiottyNetWebSocketEndpoint
    {
    public:
        using configuration_type = HttpServerConfiguration;

        HttpServer();
        ~HttpServer() override;

        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;
        HttpServer(HttpServer&&) = delete;
        HttpServer& operator=(HttpServer&&) = delete;

        KiottyNetCode  initialize(PoolHandle id, const HttpServerConfiguration& configuration);
        void           reset() noexcept;
        KiottyNetCode  run(const KiottyNetAddress& address,
                           const KiottyNetTlsServerConfiguration* tls);
        RegistryResult registry();
        NoticesResult  notices();
        HttpSite&      site();
        void           close();

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
        KiottyNetCode prepare(const HttpServerConfiguration& configuration);

        HttpSite             _site;
        std::atomic<int32_t> _runs;
    };
}
