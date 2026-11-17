#include "http/domain/usecases/kiotty_http_client_usecases.h"

#include "core/kiotty_codes.h"
#include "core/kiotty_text.h"
#include "http/data/repository/server/kiotty_http_configuration.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr size_t MAX_CLIENT_TEXT = MAX_HTTP_HEADER_BYTES;

        bool isClientMethod(KiottyNetHttpMethod method)
        {
            const int32_t index = static_cast<int32_t>(method);
            return index >= 0 && index < KIOTTY_NET_HTTP_METHOD_COUNT;
        }
    }

    ClientRequestResult clientRequestOf(const KiottyNetHttpClientRequest& request,
                                        Span<const KiottyNetHttpHeader> headers)
    {
        if (!isClientMethod(request.method)) { return ClientRequestResult::failure(INVALID); }
        const TextResult path = parseText(request.path, MAX_CLIENT_TEXT);
        if (!path) { return ClientRequestResult::failure(INVALID); }
        const Span<const char> body{ request.body.data, request.body.length };
        const ClientRequest made{ headers, path.value, body, request.method, request.type };
        return ClientRequestResult::success(made);
    }

    KiottyNetCode setHttpClientHeader(HttpClient& client, const char* name, const char* value)
    {
        const TextResult parsed_name = parseText(name, MAX_CLIENT_TEXT);
        if (!parsed_name) { return INVALID; }
        if (value == nullptr) { return client.headers().remove(parsed_name.value); }
        const TextResult parsed_value = parseText(value, MAX_CLIENT_TEXT);
        if (!parsed_value) { return INVALID; }
        return client.headers().set(parsed_name.value, parsed_value.value);
    }

    KiottyNetCode clearHttpClientHeaders(HttpClient& client)
    {
        return client.headers().clear();
    }

    void disconnectHttpClient(HttpClient& client)
    {
        client.link().disconnect();
    }

    void closeHttpClient(HttpClient& client)
    {
        client.link().close();
    }

    KiottyNetHttpReply sendHttpRequest(HttpClient& client,
                                       const ClientRequest& request,
                                       uint32_t timeout_milliseconds)
    {
        return client.exchange().send(request, timeout_milliseconds);
    }

    KiottyNetResult readHttpReplyBody(HttpClient& client,
                                      Span<char> buffer,
                                      uint32_t timeout_milliseconds)
    {
        return client.exchange().readBody(buffer, timeout_milliseconds);
    }

    KiottyNetCode beginHttpRequest(HttpClient& client,
                                   const ClientRequest& request,
                                   uint64_t body_length,
                                   uint32_t timeout_milliseconds)
    {
        return client.exchange().begin(request, body_length, timeout_milliseconds);
    }

    KiottyNetResult writeHttpRequestBody(HttpClient& client,
                                         Span<const char> data,
                                         uint32_t timeout_milliseconds)
    {
        return client.exchange().write(data, timeout_milliseconds);
    }

    KiottyNetHttpReply finishHttpRequest(HttpClient& client, uint32_t timeout_milliseconds)
    {
        return client.exchange().finish(timeout_milliseconds);
    }
}
