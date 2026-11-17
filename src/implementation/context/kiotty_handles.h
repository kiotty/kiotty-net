#pragma once

#include "base/data/repository/endpoint/kiotty_event_client.h"
#include "base/data/repository/endpoint/kiotty_server.h"
#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "http/data/repository/client/kiotty_http_client.h"
#include "websocket/data/repository/client/kiotty_websocket_client.h"
#include "websocket/data/repository/server/kiotty_websocket_server.h"

struct KiottyNetServer final : kiotty::net::implementation::Server
{
};

struct KiottyNetStreamClient final : kiotty::net::implementation::StreamClient
{
};

struct KiottyNetEventClient final : kiotty::net::implementation::EventClient
{
};

struct KiottyNetHttpServer final : kiotty::net::implementation::WebSocketServer
{
};

struct KiottyNetHttpClient final : kiotty::net::implementation::HttpClient
{
};

struct KiottyNetWebSocketClient final : kiotty::net::implementation::WebSocketClient
{
};

static_assert(sizeof(KiottyNetServer) == sizeof(kiotty::net::implementation::Server),
              "KiottyNetServer: the handle adds nothing to Server");
static_assert(sizeof(KiottyNetStreamClient) == sizeof(kiotty::net::implementation::StreamClient),
              "KiottyNetStreamClient: the handle adds nothing to StreamClient");
static_assert(sizeof(KiottyNetEventClient) == sizeof(kiotty::net::implementation::EventClient),
              "KiottyNetEventClient: the handle adds nothing to EventClient");
