#pragma once

#include "base/data/repository/endpoint/kiotty_stream_client.h"
#include "core/kiotty_deadline.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/data/state/kiotty_http_client_state.h"
#include "http/domain/entity/kiotty_http_client_request.h"
#include "http/domain/entity/kiotty_http_client_step.h"
#include "kiotty_net_base.h"

#include <cstdint>

namespace kiotty::net::implementation
{
    using TimeLeftResult = Result<uint32_t, KiottyNetCode>;
    using HeadBytesResult = Result<Span<const char>, KiottyNetCode>;

    TimeLeftResult  timeLeftOf(const Deadline& deadline);
    void            settleStop(StreamClient& line);
    HeadBytesResult writeHead(HttpClientState& state,
                              const ClientRequest& request,
                              uint64_t body_length);
    Connected       ensureConnected(StreamClient& line,
                                    const HttpClientTarget& target,
                                    const Deadline& deadline);
    KiottyNetCode   writeAll(StreamClient& line, Span<const char> data, const Deadline& deadline);
    KiottyNetCode   sendBytes(StreamClient& line,
                              Span<const char> head,
                              Span<const char> body,
                              const Deadline& deadline);
}
