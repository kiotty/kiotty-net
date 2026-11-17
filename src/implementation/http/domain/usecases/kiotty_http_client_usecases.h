#pragma once

#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/repository/client/kiotty_http_client.h"
#include "http/domain/entity/kiotty_http_client_request.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using ClientRequestResult = Result<ClientRequest, KiottyNetCode>;

    ClientRequestResult clientRequestOf(const KiottyNetHttpClientRequest& request,
                                        Span<const KiottyNetHttpHeader> headers);
    KiottyNetCode       setHttpClientHeader(HttpClient& client,
                                            const char* name,
                                            const char* value);
    KiottyNetCode       clearHttpClientHeaders(HttpClient& client);
    void                disconnectHttpClient(HttpClient& client);
    void                closeHttpClient(HttpClient& client);
    KiottyNetHttpReply  sendHttpRequest(HttpClient& client,
                                        const ClientRequest& request,
                                        uint32_t timeout_milliseconds);
    KiottyNetResult     readHttpReplyBody(HttpClient& client,
                                          Span<char> buffer,
                                          uint32_t timeout_milliseconds);
    KiottyNetCode       beginHttpRequest(HttpClient& client,
                                         const ClientRequest& request,
                                         uint64_t body_length,
                                         uint32_t timeout_milliseconds);
    KiottyNetResult     writeHttpRequestBody(HttpClient& client,
                                             Span<const char> data,
                                             uint32_t timeout_milliseconds);
    KiottyNetHttpReply  finishHttpRequest(HttpClient& client, uint32_t timeout_milliseconds);
}
