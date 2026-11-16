#pragma once

#include "core/kiotty_fixed_string.h"
#include "core/kiotty_result.h"
#include "core/kiotty_span.h"
#include "http/domain/entity/kiotty_http_request_head.h"
#include "kiotty_net_http.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::net::implementation
{
    using HostFieldResult = Result<HostField, KiottyNetCode>;
    using HeadLengthResult = Result<size_t, KiottyNetCode>;

    HostFieldResult  buildHostField(Span<const char> host, uint16_t port, uint16_t default_port);
    HostFieldResult  hostFieldOf(const KiottyNetAddress& address, uint16_t default_port);
    HeadLengthResult writeRequestHead(Span<char> buffer, const RequestHead& head);
}
