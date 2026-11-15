#include "kiotty_net_http.h"

#include "context/kiotty_context.h"
#include "context/kiotty_http_configuration.h"
#include "core/kiotty_text.h"
#include "domain/usecases/http/kiotty_http_reply_headers.h"

namespace net = kiotty::net::implementation;

namespace
{
    constexpr KiottyNetCode INVALID = KIOTTY_NET_CODE_INVALID_ARGUMENT;
    constexpr size_t        MAX_TEXT = net::MAX_HTTP_HEADER_BYTES;
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

    bool isMethod(KiottyNetHttpMethod method)
    {
        const int32_t index = static_cast<int32_t>(method);
        return index >= 0 && index < KIOTTY_NET_HTTP_METHOD_COUNT;
    }

    KiottyNetSpan spanOf(net::Span<const char> text)
    {
        const KiottyNetSpan span = { text.data, text.length };
        return span;
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
    client->link().disconnect();
}

void KiottyNet_closeHttpClient(KiottyNetHttpClientHandle client)
{
    if (client == nullptr) { return; }
    client->link().close();
}

KiottyNetCode KiottyNet_setHttpClientHeader(KiottyNetHttpClientHandle client,
                                            const char* name,
                                            const char* value)
{
    if (client == nullptr) { return INVALID; }
    const net::TextResult parsed_name = net::parseText(name, MAX_TEXT);
    if (!parsed_name) { return INVALID; }
    if (value == nullptr) { return client->headers().remove(parsed_name.value); }
    const net::TextResult parsed_value = net::parseText(value, MAX_TEXT);
    if (!parsed_value) { return INVALID; }
    return client->headers().set(parsed_name.value, parsed_value.value);
}

KiottyNetCode KiottyNet_clearHttpClientHeaders(KiottyNetHttpClientHandle client)
{
    if (client == nullptr) { return INVALID; }
    return client->headers().clear();
}

KiottyNetHttpHeader KiottyNet_makeHttpHeader(const char* name, const char* value)
{
    const KiottyNetHttpHeader unusable = { NO_TEXT, NO_TEXT };
    const net::TextResult parsed_name = net::parseText(name, MAX_TEXT);
    const net::TextResult parsed_value = net::parseText(value, MAX_TEXT);
    if (!parsed_name || !parsed_value) { return unusable; }
    const KiottyNetHttpHeader header = { spanOf(parsed_name.value), spanOf(parsed_value.value) };
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
    if (client == nullptr || request == nullptr) { return failedReply(INVALID); }
    if (headers == nullptr && header_count > 0) { return failedReply(INVALID); }
    if (!isMethod(request->method)) { return failedReply(INVALID); }
    const net::TextResult path = net::parseText(request->path, MAX_TEXT);
    if (!path) { return failedReply(INVALID); }
    const net::Span<const KiottyNetHttpHeader> extra{ headers, header_count };
    const net::Span<const char> body{ request->body.data, request->body.length };
    const net::ClientRequest made{ extra, path.value, body, request->method, request->type };
    return client->send(made, timeout_milliseconds);
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
    return client->readBody(target, timeout_milliseconds);
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
