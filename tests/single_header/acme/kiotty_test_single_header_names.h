#if !defined(KIOTTY_TEST_SINGLE_HEADER_NAMES_H)
#define KIOTTY_TEST_SINGLE_HEADER_NAMES_H

#include "acme_link.hpp"

#include <cstddef>

namespace smoke
{
    using Code = AcmeLinkCode;
    using Context = AcmeLinkContext;
    using ContextResult = AcmeLinkContextResult;
    using ServerConfiguration = AcmeLinkServerConfiguration;
    using ClientConfiguration = AcmeLinkClientConfiguration;
    using HttpLimits = AcmeLinkHttpLimits;
    using Address = AcmeLinkAddress;
    using AppContext = AcmeLinkAppContext;
    using Span = AcmeLinkSpan;
    using Result = AcmeLinkResult;
    using HttpRequest = AcmeLinkHttpRequest;
    using HttpResponseHandle = AcmeLinkHttpResponseHandle;
    using HttpReply = AcmeLinkHttpReply;
    using WebSocketHandle = AcmeLinkWebSocketHandle;
    using WebSocketContext = AcmeLinkWebSocketContext;
    using WebSocketMessage = AcmeLinkWebSocketMessage;

    using HttpServer = AcmeLink_HttpServer;
    using HttpClient = AcmeLink_HttpClient;
    using HttpLimitsBuilder = AcmeLink_HttpLimitsBuilder;
    using WebSocketRoute = AcmeLink_WebSocketRoute;
    using WebSocketClient = AcmeLink_WebSocketClient;
    template <size_t N>
    using HttpHeaders = AcmeLink_HttpHeaders<N>;
    using OwnedHttpClient = acme::link::OwnedHandle<AcmeLinkHttpClientHandle,
                                                    AcmeLink_destroyHttpClient>;

    static_assert(sizeof(OwnedHttpClient) > 0, "the C++ namespace is acme::link");
    static_assert(ACME_LINK_MAX_HOST_LENGTH > 0, "the macros start with ACME_LINK");

    constexpr Code                    SUCCESS = ACME_LINK_CODE_SUCCESS;
    constexpr AcmeLinkHttpContentType TEXT_PLAIN = ACME_LINK_HTTP_CONTENT_TYPE_TEXT_PLAIN;
    constexpr AcmeLinkWebSocketOpcode TEXT = ACME_LINK_WEB_SOCKET_OPCODE_TEXT;

    constexpr auto initializeContext = AcmeLink_initializeContext;
    constexpr auto releaseContext = AcmeLink_releaseContext;
    constexpr auto initializeServerConfiguration = AcmeLink_initializeServerConfiguration;
    constexpr auto initializeClientConfiguration = AcmeLink_initializeClientConfiguration;
    constexpr auto initializeHttpLimits = AcmeLink_initializeHttpLimits;
    constexpr auto initializeTcpAddress = AcmeLink_initializeTcpAddress;
    constexpr auto initializePipeAddress = AcmeLink_initializePipeAddress;
    constexpr auto setHttpBody = AcmeLink_setHttpBody;
    constexpr auto sendWebSocket = AcmeLink_sendWebSocket;
}

#endif
