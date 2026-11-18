#pragma once

#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_client_state.h"
#include "http/domain/entity/kiotty_http_client_request.h"
#include "http/domain/entity/kiotty_http_client_step.h"
#include "kiotty_net_http.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    class HttpClientExchange
    {
    public:
        HttpClientExchange(HttpClientState& state, StreamClient& line);

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
                                   Span<const char> head,
                                   const Deadline& deadline);
        KiottyNetCode      resend(const ClientRequest& request, const Deadline& deadline);
        KiottyNetCode      transmit(Span<const char> head,
                                    Span<const char> body,
                                    const Connected& link,
                                    const Deadline& deadline);
        KiottyNetCode      checkStart(const ClientRequest& request);
        KiottyNetCode      sendPiece(Span<const char> data, const Deadline& deadline);
        KiottyNetCode      sendEnd(const Deadline& deadline);
        KiottyNetHttpReply refusedReply(KiottyNetHttpMethod method, const Deadline& deadline);
        void               settleUpload();
        void               dropUpload();
        void               stopUpload(KiottyNetCode code);

        HttpClientState& _state;
        StreamClient&    _line;
    };
}
