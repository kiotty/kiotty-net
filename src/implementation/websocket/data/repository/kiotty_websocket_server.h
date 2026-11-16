#pragma once

#include "core/kiotty_span.h"
#include "http/data/repository/kiotty_http_server.h"
#include "kiotty_net_http.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/kiotty_websocket_endpoint.h"
#include "websocket/data/repository/kiotty_websocket_handler.h"
#include "websocket/data/repository/kiotty_websocket_socket.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class WebSocketServer : public HttpServer,
                            public WebSocketHandler,
                            public PushLane,
                            public KiottyNetWebSocketEndpoint
    {
    public:
        WebSocketServer();
        ~WebSocketServer() override;

        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;
        WebSocketServer(WebSocketServer&&) = delete;
        WebSocketServer& operator=(WebSocketServer&&) = delete;

        KiottyNetCode initialize(PoolHandle id, const HttpServerConfiguration& configuration);
        KiottyNetCode run(KiottyNetHttpServerHandle self,
                          const KiottyNetAddress& address,
                          const KiottyNetTlsServerConfiguration* tls,
                          KiottyNetAppContext app_context);

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
    };
}
