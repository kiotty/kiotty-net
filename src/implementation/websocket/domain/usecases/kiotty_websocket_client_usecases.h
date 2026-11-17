#pragma once

#include "kiotty_net_websocket.h"
#include "websocket/data/repository/client/kiotty_websocket_client.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    KiottyNetCode setWebSocketClientProtocols(WebSocketClient& client, const char* protocols);
    KiottyNetCode onWebSocketClientOpen(WebSocketClient& client,
                                        KiottyNetWebSocketClientOpenAction on_open);
    KiottyNetCode onWebSocketClientMessage(WebSocketClient& client,
                                           KiottyNetWebSocketClientMessageAction on_message);
    KiottyNetCode onWebSocketClientClose(WebSocketClient& client,
                                         KiottyNetWebSocketClientCloseAction on_close);
    KiottyNetCode onWebSocketClientError(WebSocketClient& client,
                                         KiottyNetWebSocketClientErrorAction on_error);
    KiottyNetCode runWebSocketClient(WebSocketClient& client, const ClientRun& run);
    KiottyNetCode byeWebSocketClient(WebSocketClient& client, uint16_t status, const char* reason);
    KiottyNetCode sendWebSocketClient(WebSocketClient& client,
                                      KiottyNetWebSocketOpcode opcode,
                                      KiottyNetSpan data);
}
