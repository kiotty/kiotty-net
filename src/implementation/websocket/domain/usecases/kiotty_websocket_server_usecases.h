#pragma once

#include "http/data/repository/server/kiotty_http_registry.h"
#include "kiotty_net_websocket.h"
#include "websocket/data/repository/server/kiotty_websocket_server.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    KiottyNetCode      sendWebSocket(KiottyNetWebSocketHandle websocket,
                                     KiottyNetWebSocketOpcode opcode,
                                     KiottyNetSpan data);
    KiottyNetCode      closeWebSocket(KiottyNetWebSocketHandle websocket,
                                      uint16_t status,
                                      const char* reason);
    UpgradeIndexResult addWebSocket(WebSocketServer& server, const char* path);
    KiottyNetCode      onWebSocketUpgrade(WebSocketServer& server,
                                          int32_t index,
                                          KiottyNetWebSocketUpgradeAction on_upgrade);
    KiottyNetCode      onWebSocketOpen(WebSocketServer& server,
                                       int32_t index,
                                       KiottyNetWebSocketOpenAction on_open);
    KiottyNetCode      onWebSocketMessage(WebSocketServer& server,
                                          int32_t index,
                                          KiottyNetWebSocketMessageAction on_message);
    KiottyNetCode      onWebSocketClose(WebSocketServer& server,
                                        int32_t index,
                                        KiottyNetWebSocketCloseAction on_close);
    KiottyNetCode      broadcastWebSocket(WebSocketServer& server,
                                          const char* path,
                                          KiottyNetWebSocketOpcode opcode,
                                          KiottyNetSpan data);
}
