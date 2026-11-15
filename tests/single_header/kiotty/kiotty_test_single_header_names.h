#if !defined(KIOTTY_TEST_SINGLE_HEADER_NAMES_H)
#define KIOTTY_TEST_SINGLE_HEADER_NAMES_H

#include "kiotty_net.hpp"

#include <cstddef>

namespace smoke
{
    using Code = KiottyNetCode;
    using Context = KiottyNetContext;
    using ContextResult = KiottyNetContextResult;
    using ServerConfiguration = KiottyNetServerConfiguration;
    using ClientConfiguration = KiottyNetClientConfiguration;
    using HttpLimits = KiottyNetHttpLimits;
    using Address = KiottyNetAddress;
    using AppContext = KiottyNetAppContext;
    using Span = KiottyNetSpan;
    using Result = KiottyNetResult;
    using HttpRequest = KiottyNetHttpRequest;
    using HttpResponseHandle = KiottyNetHttpResponseHandle;
    using HttpReply = KiottyNetHttpReply;
    using WebSocketHandle = KiottyNetWebSocketHandle;
    using WebSocketContext = KiottyNetWebSocketContext;
    using WebSocketMessage = KiottyNetWebSocketMessage;

    using HttpServer = KiottyNet_HttpServer;
    using HttpClient = KiottyNet_HttpClient;
    using HttpLimitsBuilder = KiottyNet_HttpLimitsBuilder;
    using WebSocketRoute = KiottyNet_WebSocketRoute;
    using WebSocketClient = KiottyNet_WebSocketClient;
    template <size_t N>
    using HttpHeaders = KiottyNet_HttpHeaders<N>;
    using OwnedHttpClient = kiotty::net::OwnedHandle<KiottyNetHttpClientHandle,
                                                     KiottyNet_destroyHttpClient>;

    static_assert(sizeof(OwnedHttpClient) > 0, "the C++ namespace is kiotty::net");
    static_assert(KIOTTY_NET_MAX_HOST_LENGTH > 0, "the macros start with KIOTTY_NET");

    constexpr Code                     SUCCESS = KIOTTY_NET_CODE_SUCCESS;
    constexpr KiottyNetHttpContentType TEXT_PLAIN = KIOTTY_NET_HTTP_CONTENT_TYPE_TEXT_PLAIN;
    constexpr KiottyNetWebSocketOpcode TEXT = KIOTTY_NET_WEB_SOCKET_OPCODE_TEXT;

    constexpr auto initializeContext = KiottyNet_initializeContext;
    constexpr auto releaseContext = KiottyNet_releaseContext;
    constexpr auto initializeServerConfiguration = KiottyNet_initializeServerConfiguration;
    constexpr auto initializeClientConfiguration = KiottyNet_initializeClientConfiguration;
    constexpr auto initializeHttpLimits = KiottyNet_initializeHttpLimits;
    constexpr auto initializeTcpAddress = KiottyNet_initializeTcpAddress;
    constexpr auto initializePipeAddress = KiottyNet_initializePipeAddress;
    constexpr auto setHttpBody = KiottyNet_setHttpBody;
    constexpr auto sendWebSocket = KiottyNet_sendWebSocket;
}

#endif
