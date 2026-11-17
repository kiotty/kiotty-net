#pragma once

#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_client_state.h"
#include "http/domain/entity/kiotty_http_client_step.h"
#include "kiotty_net_http.h"

namespace kiotty::net::implementation
{
    ReplyHead          receiveReply(StreamClient& line,
                                    HttpClientState& state,
                                    KiottyNetHttpMethod method,
                                    const Deadline& deadline);
    KiottyNetResult    pumpBody(StreamClient& line,
                                HttpClientState& state,
                                Span<char> buffer,
                                const Deadline& deadline);
    KiottyNetHttpReply replyOf(StreamClient& line, HttpClientState& state, KiottyNetCode code);
    void               dropUnreadReply(StreamClient& line, HttpClientState& state);
}
