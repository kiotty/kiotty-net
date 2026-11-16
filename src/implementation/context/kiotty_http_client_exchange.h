#pragma once

#include "core/kiotty_deadline.h"
#include "core/kiotty_span.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    class HttpClient;
    struct ClientRequest;

    class HttpClientExchange
    {
    public:
        explicit HttpClientExchange(HttpClient& client);

        KiottyNetHttpReply send(const ClientRequest& request, uint32_t timeout_milliseconds);
        KiottyNetResult    readBody(Span<char> buffer, uint32_t timeout_milliseconds);
        KiottyNetCode      begin(const ClientRequest& request,
                                 uint64_t body_length,
                                 uint32_t timeout_milliseconds);
        KiottyNetResult    write(Span<const char> data, uint32_t timeout_milliseconds);
        KiottyNetHttpReply finish(uint32_t timeout_milliseconds);

    private:
        struct Attempt
        {
            KiottyNetCode code;
            bool          is_stale;
        };

        KiottyNetHttpReply run(const ClientRequest& request, uint32_t timeout_milliseconds);
        Attempt            attempt(const ClientRequest& request,
                                   size_t head_length,
                                   const Deadline& deadline);
        KiottyNetCode      resend(const ClientRequest& request, const Deadline& deadline);
        KiottyNetCode      checkStart(const ClientRequest& request);
        KiottyNetCode      sendPiece(Span<const char> data, const Deadline& deadline);
        KiottyNetCode      sendEnd(const Deadline& deadline);
        void               settleUpload();
        void               dropUpload();

        HttpClient& _client;
    };
}
