#include "kiotty_net_http.h"

#include "context/kiotty_context.h"
#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/header/kiotty_http_reply_headers_parser.h"
#include "http/data/repository/header/kiotty_http_token_rules.h"
#include "http/data/repository/server/kiotty_http_configuration.h"
#include "http/domain/usecases/kiotty_http_client_usecases.h"

namespace net = kiotty::net::implementation;

namespace
{
    using net::INVALID;
    constexpr size_t        MAX_TEXT = net::MAX_HTTP_HEADER_BYTES;
    constexpr size_t        MAX_FILE_PATH_TEXT = KIOTTY_NET_MAX_PATH_LENGTH - 1;
    constexpr KiottyNetSpan NO_TEXT = { nullptr, 0 };

    KiottyNetHttpReply failedReply(KiottyNetCode code)
    {
        const KiottyNetHttpReply reply = { code, 0, nullptr, 0 };
        return reply;
    }

    void chooseLimits(const KiottyNetHttpLimits* limits, KiottyNetHttpLimits& chosen)
    {
        if (limits == nullptr)
        {
            (void)net::initializeHttpLimits(chosen);
            return;
        }
        chosen = *limits;
    }

    KiottyNetHttpClientRequest requestWithBody(KiottyNetHttpMethod method,
                                               const char* path,
                                               KiottyNetHttpContentType type,
                                               KiottyNetSpan body)
    {
        KiottyNetHttpClientRequest request = KiottyNet_makeHttpClientRequest(method, path);
        request.type = type;
        request.body = body;
        return request;
    }

    net::ClientRequestResult requestOf(const KiottyNetHttpClientRequest* request,
                                       const KiottyNetHttpHeader* headers,
                                       size_t header_count)
    {
        using RequestResult = net::ClientRequestResult;
        if (request == nullptr) { return RequestResult::failure(INVALID); }
        if (headers == nullptr && header_count > 0) { return RequestResult::failure(INVALID); }
        const net::Span<const KiottyNetHttpHeader> extra{ headers, header_count };
        return net::clientRequestOf(*request, extra);
    }

    net::Span<const KiottyNetHttpHeader> replyHeadersOf(const KiottyNetHttpReply& reply)
    {
        if (reply.headers == nullptr) { return net::Span<const KiottyNetHttpHeader>{ nullptr, 0 }; }
        return net::Span<const KiottyNetHttpHeader>{ reply.headers, reply.header_count };
    }
}

KiottyNetHttpClientResult KiottyNet_createHttpClient(
    KiottyNetContext context,
    const KiottyNetClientConfiguration* configuration,
    const KiottyNetHttpLimits* limits)
{
    KiottyNetHttpClientResult result = { INVALID, nullptr };
    if (context == nullptr || configuration == nullptr) { return result; }
    net::HttpClientConfiguration made{ *configuration, KiottyNetHttpLimits{} };
    chooseLimits(limits, made.limits);
    const net::HttpClientResult created = context->create(made);
    result.code = created.code;
    if (created) { result.handle = &created.value(); }
    return result;
}

void KiottyNet_destroyHttpClient(KiottyNetContext context, KiottyNetHttpClientHandle client)
{
    if (context == nullptr || client == nullptr) { return; }
    context->destroy(*client);
}

KiottyNetCode KiottyNet_connectHttpClient(KiottyNetHttpClientHandle client,
                                          const KiottyNetAddress* address,
                                          const KiottyNetTlsClientConfiguration* tls)
{
    if (client == nullptr || address == nullptr) { return INVALID; }
    const KiottyNetAddress copied = *address;
    return client->connect(copied, tls);
}

void KiottyNet_disconnectHttpClient(KiottyNetHttpClientHandle client)
{
    if (client == nullptr) { return; }
    net::disconnectHttpClient(*client);
}

void KiottyNet_closeHttpClient(KiottyNetHttpClientHandle client)
{
    if (client == nullptr) { return; }
    net::closeHttpClient(*client);
}

KiottyNetCode KiottyNet_setHttpClientHeader(KiottyNetHttpClientHandle client,
                                            const char* name,
                                            const char* value)
{
    if (client == nullptr) { return INVALID; }
    return net::setHttpClientHeader(*client, name, value);
}

KiottyNetCode KiottyNet_clearHttpClientHeaders(KiottyNetHttpClientHandle client)
{
    if (client == nullptr) { return INVALID; }
    return net::clearHttpClientHeaders(*client);
}

KiottyNetHttpHeader KiottyNet_makeHttpHeader(const char* name, const char* value)
{
    const KiottyNetHttpHeader unusable = { NO_TEXT, NO_TEXT };
    const net::TextResult parsed_name = net::parseText(name, MAX_TEXT);
    const net::TextResult parsed_value = net::parseText(value, MAX_TEXT);
    if (!parsed_name || !parsed_value) { return unusable; }
    const KiottyNetHttpHeader header = { net::spanOf(parsed_name.value), net::spanOf(parsed_value.value) };
    return header;
}

KiottyNetHttpClientRequest KiottyNet_makeHttpClientRequest(KiottyNetHttpMethod method,
                                                           const char* path)
{
    KiottyNetHttpClientRequest request = {};
    request.method = method;
    request.path = path;
    request.type = KIOTTY_NET_HTTP_CONTENT_TYPE_OCTET_STREAM;
    request.body = NO_TEXT;
    return request;
}

KiottyNetHttpReply KiottyNet_sendHttpRequest(KiottyNetHttpClientHandle client,
                                             const KiottyNetHttpClientRequest* request,
                                             const KiottyNetHttpHeader* headers,
                                             size_t header_count,
                                             uint32_t timeout_milliseconds)
{
    if (client == nullptr) { return failedReply(INVALID); }
    const net::ClientRequestResult made = requestOf(request, headers, header_count);
    if (!made) { return failedReply(made.code); }
    return net::sendHttpRequest(*client, made.value, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_getHttp(KiottyNetHttpClientHandle client,
                                     const char* path,
                                     const KiottyNetHttpHeader* headers,
                                     size_t header_count,
                                     uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_GET, path);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_postHttp(KiottyNetHttpClientHandle client,
                                      const char* path,
                                      const KiottyNetHttpHeader* headers,
                                      size_t header_count,
                                      KiottyNetHttpContentType type,
                                      KiottyNetSpan body,
                                      uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        requestWithBody(KIOTTY_NET_HTTP_METHOD_POST, path, type, body);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_putHttp(KiottyNetHttpClientHandle client,
                                     const char* path,
                                     const KiottyNetHttpHeader* headers,
                                     size_t header_count,
                                     KiottyNetHttpContentType type,
                                     KiottyNetSpan body,
                                     uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        requestWithBody(KIOTTY_NET_HTTP_METHOD_PUT, path, type, body);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_deleteHttp(KiottyNetHttpClientHandle client,
                                        const char* path,
                                        const KiottyNetHttpHeader* headers,
                                        size_t header_count,
                                        uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_DELETE, path);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_patchHttp(KiottyNetHttpClientHandle client,
                                       const char* path,
                                       const KiottyNetHttpHeader* headers,
                                       size_t header_count,
                                       KiottyNetHttpContentType type,
                                       KiottyNetSpan body,
                                       uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        requestWithBody(KIOTTY_NET_HTTP_METHOD_PATCH, path, type, body);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_headHttp(KiottyNetHttpClientHandle client,
                                      const char* path,
                                      const KiottyNetHttpHeader* headers,
                                      size_t header_count,
                                      uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_HEAD, path);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_optionsHttp(KiottyNetHttpClientHandle client,
                                         const char* path,
                                         const KiottyNetHttpHeader* headers,
                                         size_t header_count,
                                         uint32_t timeout_milliseconds)
{
    const KiottyNetHttpClientRequest request =
        KiottyNet_makeHttpClientRequest(KIOTTY_NET_HTTP_METHOD_OPTIONS, path);
    return KiottyNet_sendHttpRequest(client, &request, headers, header_count, timeout_milliseconds);
}

KiottyNetResult KiottyNet_readHttpReplyBody(KiottyNetHttpClientHandle client,
                                            void* buffer,
                                            size_t length,
                                            uint32_t timeout_milliseconds)
{
    const KiottyNetResult invalid = { 0, INVALID };
    if (client == nullptr || buffer == nullptr || length == 0) { return invalid; }
    const net::Span<char> target{ static_cast<char*>(buffer), length };
    return net::readHttpReplyBody(*client, target, timeout_milliseconds);
}

KiottyNetCode KiottyNet_beginHttpRequest(KiottyNetHttpClientHandle client,
                                         const KiottyNetHttpClientRequest* request,
                                         const KiottyNetHttpHeader* headers,
                                         size_t header_count,
                                         uint64_t body_length,
                                         uint32_t timeout_milliseconds)
{
    if (client == nullptr) { return INVALID; }
    const net::ClientRequestResult made = requestOf(request, headers, header_count);
    if (!made) { return made.code; }
    return net::beginHttpRequest(*client, made.value, body_length, timeout_milliseconds);
}

KiottyNetResult KiottyNet_writeHttpRequestBody(KiottyNetHttpClientHandle client,
                                               const void* data,
                                               size_t length,
                                               uint32_t timeout_milliseconds)
{
    const KiottyNetResult invalid = { 0, INVALID };
    if (client == nullptr) { return invalid; }
    if (data == nullptr && length > 0) { return invalid; }
    const net::Span<const char> piece{ static_cast<const char*>(data), length };
    return net::writeHttpRequestBody(*client, piece, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_finishHttpRequest(KiottyNetHttpClientHandle client,
                                               uint32_t timeout_milliseconds)
{
    if (client == nullptr) { return failedReply(INVALID); }
    return net::finishHttpRequest(*client, timeout_milliseconds);
}

KiottyNetHttpReply KiottyNet_sendHttpFile(KiottyNetHttpClientHandle client,
                                          const KiottyNetHttpClientRequest* request,
                                          const KiottyNetHttpHeader* headers,
                                          size_t header_count,
                                          const char* path,
                                          uint32_t timeout_milliseconds)
{
    if (client == nullptr) { return failedReply(INVALID); }
    const net::TextResult file = net::parseText(path, MAX_FILE_PATH_TEXT);
    if (!file) { return failedReply(file.code); }
    if (file.value.empty()) { return failedReply(INVALID); }
    const net::ClientRequestResult made = requestOf(request, headers, header_count);
    if (!made) { return failedReply(made.code); }
    return net::sendHttpFile(*client, made.value, file.value, timeout_milliseconds);
}

KiottyNetSpan KiottyNet_findHttpReplyHeader(const KiottyNetHttpReply* reply, const char* name)
{
    if (reply == nullptr) { return NO_TEXT; }
    const net::TextResult wanted = net::parseText(name, MAX_TEXT);
    if (!wanted) { return NO_TEXT; }
    return net::findReplyHeader(replyHeadersOf(*reply), wanted.value);
}

uint32_t KiottyNet_getHttpAllowedMethods(const KiottyNetHttpReply* reply)
{
    if (reply == nullptr) { return 0; }
    return net::allowedMethodsOf(replyHeadersOf(*reply));
}
