#pragma once

#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_client_state.h"
#include "http/domain/entity/kiotty_http_client_step.h"
#include "http/domain/entity/kiotty_http_reply_framing.h"
#include "kiotty_net_http.h"

namespace kiotty::net::implementation
{
    using ReplyPhaseResult = Result<ReplyPhase, KiottyNetCode>;

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
    bool               hasStagedReply(StreamClient& line);
    ReplyPhaseResult   receiveOneReply(StreamClient& line,
                                       HttpClientState& state,
                                       KiottyNetHttpMethod method,
                                       const Deadline& deadline);
}
